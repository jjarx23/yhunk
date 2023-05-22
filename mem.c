#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>

typedef struct blk_ blk_t;

struct head_
{
#ifndef SMALL_META
    blk_t *nxt;
    size_t sze;
    char inuse;
#else
    void *meta;
#endif
};

struct blk_
{
    struct head_ head;
    void *pntr;
#ifdef SMALL_META
    void *prev;
#endif
};

const int first_fit = 1;
const int best_fit = 2;
const int next_fit = 3;
const int search_method = first_fit;

int list_index = -1;
size_t gen_size = 17 * sizeof(void *);

blk_t *segregated_list[17];
blk_t *segregated_tops[17];
blk_t *segregated_last_fits[17];

char mutex_available = 0;
pthread_mutex_t lock;

void *limit = 0;
void *cbrk = 0, *cmbrk = 0;
size_t heap_size = 1024 * 1024 * 4;

static inline blk_t *BLK(void *B)
{
    return (blk_t *)(B);
}


static inline size_t META_RESIZE(size_t n)
{
    return sizeof(struct head_) + (n);
}

#ifndef SMALL_META
static inline size_t MIN_SIZE()
{
    return META_RESIZE(sizeof(void *));
}

static inline blk_t *NEXT(blk_t *b)
{
    return BLK(b->head.nxt);
}

static inline void SET_NEXT(blk_t *b, blk_t *s)
{
    b->head.nxt = s;
}

static inline size_t SIZE(blk_t *b)
{
    return BLK(b)->head.sze;
}

static inline void SET_SIZE(blk_t *b, size_t s)
{
    BLK(b)->head.sze = s;
}

static inline void SET_SIZE_USE(blk_t *b, size_t s)
{
    SET_SIZE(b, s);
    b->head.inuse = 1;
}

static inline char IN_USE(blk_t *b)
{
    return BLK(b)->head.inuse;
}

static inline void TOGGLE_USE(blk_t *b)
{
    BLK(b)->head.inuse ^= 1;
}

static inline void *PREV(blk_t *b)
{
    return BLK(b)->pntr;
}

static inline void SET_PREV(blk_t *b, blk_t *s)
{
    ((blk_t **)&BLK(b)->pntr)[0] = s;
}

#else

size_t min_alloc = 2 * sizeof(void *);

static inline size_t MIN_SIZE()
{
    return META_RESIZE(min_alloc);
}

static inline void *PREV(blk_t *b)
{
    return BLK(b)->prev;
}

static inline void SET_PREV(blk_t *b, blk_t *s)
{
    b->prev = s;
}

static inline size_t SIZE(blk_t *b)
{
    return ((size_t)BLK(b)->head.meta & ~1L);
}

static inline void SET_SIZE(blk_t *b, size_t s)
{
    ((size_t *)&BLK(b)->head.meta)[0] = s;
}

static inline void SET_SIZE_USE(blk_t *b, size_t s)
{
    SET_SIZE(b, s | 1L);
}

static inline char IN_USE(blk_t *b)
{
    return ((size_t)BLK(b)->head.meta & 1L);
}

static inline void TOGGLE_USE(blk_t *b)
{
    SET_SIZE(b, IN_USE(b) ? SIZE(b) : SIZE(b) | 1L);
}

static inline blk_t *NEXT(blk_t *b)
{
    return BLK(b)->pntr;
}

static inline void SET_NEXT(blk_t *b, blk_t *s)
{
    ((blk_t **)&BLK(b)->pntr)[0] = s;
}

#endif

static inline void *MEM(blk_t *b)
{
    return (void *)&BLK(b)->pntr;
}

static inline size_t ALIGN(size_t n)
{
    return (n + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
}

static inline char CAN_SPLIT(blk_t *b, size_t sz)
{
    return (SIZE(b) - sz) >= MIN_SIZE();
}

static inline blk_t *GET_HEAD(void *b)
{
    return (blk_t *)(((char *)b) - sizeof(struct head_));
}

static inline blk_t *GET_SUCCESSIVE_HEAD(blk_t *b)
{
    return BLK((char *)&BLK(b)->pntr + SIZE(b));
}

static inline char CAN_MERGE(blk_t *b)
{
    return (list_index >= 0) && (GET_SUCCESSIVE_HEAD(b) != limit) && (!IN_USE(GET_SUCCESSIVE_HEAD(b)));
}

static inline int LIST_INDEX(size_t sz)
{
    return (sz / sizeof(void *)) - 1;
}

static inline blk_t *FREELIST()
{
    return segregated_list[list_index];
}

static inline void SET_FREELIST(blk_t *value)
{
    segregated_list[list_index] = value;
}

static inline blk_t *FREETOP()
{
    return segregated_tops[list_index];
}

static inline void SET_FREETOP(blk_t *value)
{
    segregated_tops[list_index] = value;
}

static inline blk_t *LAST_FIT()
{
    return segregated_last_fits[list_index];
}

static inline void SET_LAST_FIT(blk_t *value)
{
    segregated_last_fits[list_index] = value;
}

static inline clock_t TIMER()
{
    return clock();
}

static inline double TTIME(clock_t t)
{
    return (((double)(clock() - t)) / CLOCKS_PER_SEC);
}

static inline void LOCK()
{
    if (mutex_available)
        pthread_mutex_lock(&lock);
}

static inline void UNLOCK()
{
    if (mutex_available)
        pthread_mutex_unlock(&lock);
}

static void afree(void *b);

static void *csbrk(size_t s)
{
    if (!s)
        return cbrk;
    void *ptr = cbrk + s;
    if (ptr > cmbrk)
        return (void *)-1;
    void *_ = cbrk;
    cbrk = ptr;
    return _;
}

static void *get_mem_chunk(size_t sz)
{
    blk_t *brk = csbrk(0);
    if (csbrk(sz) == (void *)-1)
        return 0;
    else
        limit = ((char *)brk) + sz;
    return brk;
}
static inline void adjust_free_list(blk_t *b, blk_t *prev, blk_t *next)
{
    if (b == FREELIST())
    {
        SET_FREELIST(prev);
        if (!FREELIST)
            SET_FREELIST(next);
    }
    if (b == FREETOP())
    {
        SET_FREETOP(next);
        if (!FREETOP())
            SET_FREETOP(prev);
    }
}
static inline void *fix_free_list(blk_t *b, blk_t *p, blk_t *n)
{
    if (!IN_USE(b))
        TOGGLE_USE(b);
    adjust_free_list(b, p, n);
    if (n)
    {
        blk_t *_ = PREV(NEXT(b));
        SET_PREV(n, p);
    }
    if (p)
    {
        blk_t *_ = PREV(b);
        SET_NEXT(p, n);
        int i = 0;
    }
    // PREV(b->head.next) =
    // PREV(b)
    return b;
}
static void *reuse_block(size_t size)
{
    if (!FREELIST())
        return 0;
    // blk_t *p=0;
    switch (search_method)
    {
    case first_fit:
    {
        blk_t *b = FREELIST();
        while (b)
        {
            if (SIZE(b) >= size && !IN_USE(b))
            {
                // b->head.in_use = 1;
                return fix_free_list((blk_t *)b, PREV(b), NEXT(b));
            }
            // p=b;
            b = NEXT(b);
        }
    }
    break;
    case best_fit:
    {
        blk_t *b = FREELIST(),
              *res = 0;
        while (b)
        {
            BFAM(0, PREV(b), b, NEXT(b));
            // BFAM(1,p, b, NEXT(b));
            if (SIZE(b) >= size && !IN_USE(b))
            {
                if (SIZE(b) == size)
                {
                    return fix_free_list((blk_t *)b, PREV(b), NEXT(b));
                }
                if (res ? SIZE(res) > SIZE(b) : 1)
                    res = b;
            }
            // p=b;
            b = NEXT(b);
            // printf("%p\n", b);
        }
        if (res)
        {
            return fix_free_list((blk_t *)res, PREV(res), NEXT(res));
        }
        return res;
    }
    break;
    case next_fit:
    {
        blk_t *b = (LAST_FIT ? LAST_FIT : FREELIST());
        while (b)
        {
            if (SIZE(b) >= size && !IN_USE(b))
            {
                SET_LAST_FIT(NEXT(b));
                return fix_free_list((blk_t *)b, PREV(b), NEXT(b));
            }
            // p=b;
            b = NEXT(b) ? NEXT(b) : FREELIST();
            if ((b == (FREELIST()) && LAST_FIT == 0) || ((blk_t *)b) == LAST_FIT)
            {
                return 0;
            }
        }
    }
    break;
    default:
        printf("unknown method\n");
    }
    return 0;
}
static void split(blk_t *b, size_t sz)
{
    blk_t *bb = (blk_t *)(((char *)(b)) + META_RESIZE(sz));
    size_t osize;
    SET_SIZE_USE(bb, osize = SIZE(b) - META_RESIZE(sz));
    SET_SIZE_USE(b, sz);
    afree(MEM(bb));
}
static inline void set_list_index(size_t sz)
{
    if (sz >= gen_size)
    {
        list_index = 16;
    }
    else
    {
        list_index = LIST_INDEX(sz);
    }
}
static void *alloc(size_t sz)
{
    // UNLOCK;
    if (!sz)
        return 0;
    // Align size to ensure proper memory alignment
    sz = ALIGN(sz);
#ifdef SMALL_META
    if (sz < min_alloc)
        sz = min_alloc;
#endif
    if (list_index < 0)
    {
        if (!pthread_mutex_init(&lock, NULL))
            mutex_available = 1;
        if (mutex_available)
            pthread_mutex_lock(&lock);
        if (!cbrk)
        {
            cbrk = mmap(0, heap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            cmbrk = cbrk + heap_size;
        }
        memset(segregated_list, 0, sizeof(segregated_list));
        memset(segregated_tops, 0, sizeof(segregated_tops));
        memset(segregated_last_fits, 0, sizeof(segregated_last_fits));
        if (mutex_available)
            pthread_mutex_unlock(&lock);
    }
    set_list_index(sz);

    if (!FREELIST() && segregated_list[16])
        list_index = 16;

    // printf("alloc %zu\n", sz);
    blk_t *b;
    if ((b = reuse_block(sz)))
    {
        if (CAN_SPLIT(b, sz))
            split(b, sz);
        // UNLOCK;
        return MEM(b);
    }
    b = get_mem_chunk(META_RESIZE(sz));
    if (!b)
    {
        fwrite("OOM...\n", 1, 7, stderr);
        // UNLOCK;
        return 0;
    }
    // b->head.in_use = 1;
    SET_SIZE_USE(b, sz);
    // UNLOCK;
    return MEM(b);
}
static void merge(blk_t *b)
{
    blk_t *nb = GET_SUCCESSIVE_HEAD(b);
    set_list_index(SIZE(nb));
    SET_NEXT(b, NEXT(nb));
    size_t _ = SIZE(b), k = META_RESIZE(SIZE(nb)),
           new_size = SIZE(b) + META_RESIZE(SIZE(nb));
    SET_SIZE(b, new_size);
    fix_free_list(nb, PREV(nb), NEXT(nb));
    // adjust_free_list(nb, b, NEXT(nb));
    //  printf("merge %zu, %zu+%zu\n", b->head.size, META_RESIZE(nb->head.size), _);
}
static void afree(void *b)
{
    // UNLOCK;
    if (!b)
        return;
    blk_t *h = GET_HEAD(b);
    if (!IN_USE(h))
    {
        // UNLOCK;
        return;
    }
    TOGGLE_USE(h);
    if (CAN_MERGE(h))
        merge(h);

    /*if (!heap_start)
    heap_start = b;
else
    top->head.next = b;
top = b;
b->head.next = 0;*/
    set_list_index(SIZE(h));
    if (!FREELIST())
    {
        SET_FREELIST(h);
    }
    else
    {
        SET_NEXT(FREETOP(), h);
    }

    SET_PREV(h, FREETOP());
    //_ = PREV(h);
    SET_FREETOP(h);
    SET_NEXT(h, 0);
    // UNLOCK;
}
static void *re_alloc(void *b, size_t sz)
{
    // UNLOCK;
    if (!b)
    {
        // UNLOCK;
        return 0;
    }
    else
    {
        sz = ALIGN(sz);
        blk_t *h = GET_HEAD(b);
        if (sz <= SIZE(h))
        {
            // UNLOCK;
            return b;
        }
        else if (b + SIZE(h) == limit)
        {
            size_t rem = sz - SIZE(h);
            get_mem_chunk(rem);
            SET_SIZE_USE(h, sz);
            // UNLOCK;
            return b;
        }
        else
        {
            void *n = alloc(sz);
            memcpy(n, b, SIZE(h));
            afree(b);
            // UNLOCK;
            return n;
        }
    }
}
void *malloc(size_t sz)
{
    LOCK;
    void *_ = alloc(sz);
    UNLOCK;
    return _;
}
void *calloc(size_t n, size_t s)
{
    LOCK;
    size_t size;
    void *block;
    if (!n || !s)
        return NULL;
    size = n * s;
    /* check mul overflow */
    if (s != size / n)
        return NULL;
    block = malloc(size);
    if (!block)
        return NULL;
    memset(block, 0, size);
    UNLOCK;
    return block;
}
void free(void *b)
{
    LOCK;
    afree(b);
    UNLOCK;
    return;
}
void *realloc(void *b, size_t s)
{
    return re_alloc(b, s);
}
#ifndef MEM_LIB

#endif