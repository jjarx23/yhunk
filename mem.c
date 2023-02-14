#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mem.h"

typedef struct blk_ blk_t;
struct head_
{
    blk_t *next;
    size_t size;
    char in_use;
};
struct blk_
{
    struct head_ head;
    char ptr;
};

#define ALIGN(n) (n + sizeof(void *) - 1) & ~(sizeof(void *) - 1)
#define META_RESIZE(n) (sizeof(struct head_) + n)
#define MIN_SIZE (META_RESIZE(sizeof(void *)))
#define CAN_SPLIT(b, sz) ((((blk_t *)b)->head.size - sz) >= MIN_SIZE)
#define GET_HEAD(b) ((blk_t *)(((char *)b) - sizeof(struct head_)))
#define GET_SUCCESSIVE_HEAD(b) ((blk_t *)((&((blk_t *)b)->ptr) + b->head.size))
#define CAN_MERGE(b) ((list_index >= 0) && (GET_SUCCESSIVE_HEAD(b) != limit) && (GET_SUCCESSIVE_HEAD(b)->head.in_use == 0))
#define PREV(b) (((blk_t **)(&((blk_t *)(b))->ptr))[0])
#define LIST_INDEX(sz) (sz / sizeof(void *) - 1)

#define FREELIST segregated_list[list_index]
#define FREETOP segregated_tops[list_index]
#define LAST_FIT segregated_last_fits[list_index]

#define FIRST_FIT 0
#define BEST_FIT 01
#define NEXT_FIT 0

#define TIMER clock()
#define TTIME(t) (((double)(clock() - t)) / CLOCKS_PER_SEC)

// blk_t *heap_start = 0;
// blk_t *top;

int list_index = -1;

blk_t *segregated_list[17];
blk_t *segregated_tops[17];
blk_t *segregated_last_fits[17];

void *limit = 0;

static void afree(void *b);

static void *get_mem_chunk(size_t sz)
{
    blk_t *brk = sbrk(0);
    if (sbrk(sz) == (void *)-1)
        return 0;
    else
        limit = ((char *)brk) + sz;
    return brk;
}
static inline void adjust_free_list(blk_t *b, blk_t *prev, blk_t *next)
{
    if (b == FREELIST)
    {
        FREELIST = prev;
        if (!FREELIST)
            FREELIST = next;
    }
    if (b == FREETOP)
    {
        FREETOP = next;
        if (!FREETOP)
            FREETOP = prev;
    }
}
static inline void *fix_free_list(blk_t *b)
{
    b->head.in_use = 1;
    adjust_free_list(b, PREV(b), b->head.next);
    if (b->head.next)
    {
        blk_t *_ = PREV(b->head.next);
        PREV(b->head.next) = PREV(b);
    }
    if (PREV(b))
    {
        blk_t *_ = PREV(b);
        PREV(b)->head.next = b->head.next;
        int i = 0;
    }
    // PREV(b->head.next) =
    // PREV(b)
    return b;
}
static void *reuse_block(size_t size)
{
    if (!FREELIST)
        return 0;
    if (FIRST_FIT)
    {
        struct head_ *b = (struct head_ *)FREELIST;
        while (b)
        {
            if (b->size >= size && !b->in_use)
            {
                // b->in_use = 1;
                return fix_free_list((blk_t *)b);
            }
            b = (struct head_ *)b->next;
        }
    }
    else if (BEST_FIT)
    {
        struct head_ *b = (struct head_ *)FREELIST,
                     *res = 0;
        while (b)
        {
            if (b->size >= size && !b->in_use)
            {
                if (b->size == size)
                {
                    return fix_free_list((blk_t *)b);
                }
                if (res ? res->size > b->size : 1)
                    res = b;
            }
            b = (struct head_ *)b->next;
        }
        if (res)
        {
            return fix_free_list((blk_t *)res);
        }
        return res;
    }
    else if (NEXT_FIT)
    {
        struct head_ *b = (struct head_ *)(LAST_FIT ? LAST_FIT : FREELIST);
        while (b)
        {
            if (b->size >= size && !b->in_use)
            {
                LAST_FIT = b->next;
                return fix_free_list((blk_t *)b);
            }
            b = (struct head_ *)(b->next ? b->next : FREELIST);
            if ((b == ((struct head_ *)FREELIST) && LAST_FIT == 0) || ((blk_t *)b) == LAST_FIT)
            {
                return 0;
            }
        }
    }
    return 0;
}
static void split(blk_t *b, size_t sz)
{
    blk_t *bb = (blk_t *)(((char *)(b)) + META_RESIZE(sz));
    bb->head.size = b->head.size - META_RESIZE(sz);
    b->head.size = sz;
    bb->head.in_use = 1;
    afree(&bb->ptr);
}
static inline void set_list_index(size_t sz)
{
    list_index = LIST_INDEX(sz);
    list_index = list_index > 16 ? 16 : list_index;
}
static void *alloc(size_t sz)
{
    if (!sz)
        return 0;
    sz = ALIGN(sz);
    if (list_index < 0)
    {
        memset(segregated_list, 0, sizeof(segregated_list));
        memset(segregated_tops, 0, sizeof(segregated_tops));
        memset(segregated_last_fits, 0, sizeof(segregated_last_fits));
    }
    set_list_index(sz);
    // printf("alloc %zu\n", sz);
    blk_t *b;
    if ((b = reuse_block(sz)))
    {
        if (CAN_SPLIT(b, sz))
            split(b, sz);
        return &b->ptr;
    }
    b = get_mem_chunk(META_RESIZE(sz));
    if (!b)
    {
        fwrite("OOM...\n", 1, 7, stderr);
        exit(-3);
    }
    b->head.in_use = 1;
    b->head.size = sz;
    return &b->ptr;
}
static void merge(blk_t *b)
{
    blk_t *nb = GET_SUCCESSIVE_HEAD(b);
    b->head.next = nb->head.next;
    size_t _ = b->head.size, k = META_RESIZE(nb->head.size);
    b->head.size += META_RESIZE(nb->head.size);
    adjust_free_list(nb, b, nb->head.next);
    // printf("merge %zu, %zu+%zu\n", b->head.size, META_RESIZE(nb->head.size), _);
}
static void afree(void *b)
{
    if (!b)
        return;
    blk_t *h = GET_HEAD(b);
    if (!h->head.in_use)
        return;
    h->head.in_use = 0;
    if (CAN_MERGE(h))
        merge(h);
    else
    {
        /*if (!heap_start)
        heap_start = b;
    else
        top->head.next = b;
    top = b;
    b->head.next = 0;*/

        if (!FREELIST)
        {
            FREELIST = h;
        }
        else
        {
            FREETOP->head.next = h;
        }

        PREV(h) = FREETOP;
        //_ = PREV(h);
        FREETOP = h;
        h->head.next = 0;
    }
}
int main()
{
    void *b = alloc(3);
    afree(b);
    void *c = alloc(25);
    afree(c);
    b = alloc(3);
    afree(b);
    c = alloc(20);
    afree(c);
    exit(9);
    // printf("%i ; %zu\n", 3, get_head(b)->head.sz);

    clock_t t;

    t = TIMER;
    // work here
    int i = 100000;
    int j = 0;
    void *_[i];
    while (i)
    {
        i--;
        _[j] = (malloc((100000 - i) % 83 + 3));
        if (i % 13)
            free(_[j]);
        else
        {
            j++;
        }
    }
    while (j)
    {
        j--;
        free(_[j]);
    }

    printf(" took %f seconds to execute \n", TTIME(t));

    t = TIMER;

    // work here
    i = 100000;
    j = 0;
    while (i)
    {
        i--;
        _[j] = (alloc((100000 - i) % 83 + 3));
        if (i % 13)
            afree(_[j]);
        else
        {
            j++;
        }
    }
    while (j)
    {
        j--;
        afree(_[j]);
    }

    printf(" took %f seconds to execute \n", TTIME(t));
    return 0;
}
