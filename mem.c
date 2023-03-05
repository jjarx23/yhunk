#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "mem.h"

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
    void* pntr;
#ifdef SMALL_META
    void *prev;
#endif
};

#define BLK(B)((blk_t*)(B))
#ifndef SMALL_META
#define MIN_SIZE (META_RESIZE(sizeof(void *)))
#define NEXT(b)(BLK(b)->head.nxt)
#define SET_NEXT(b, s)(BLK(b)->head.nxt=(s))

#define SIZE(b)(BLK(b)->head.sze)
#define SET_SIZE(b, s)(BLK(b)->head.sze=(s))
#define SET_SIZE_USE(b, s)SET_SIZE(b,s);b->head.inuse=1;

#define IN_USE(b)(BLK(b)->head.inuse)
#define TOGGLE_USE(b)(BLK(b)->head.inuse^=1)

#define PREV(b)(BLK(b)->pntr)
#define SET_PREV(b,s) (((blk_t **)&BLK(b)->pntr)[0]=(s))
#else
size_t min_alloc = 2*sizeof(void*);
#define MIN_SIZE (META_RESIZE(min_alloc))
#define PREV(b)(BLK(b)->prev)
#define SET_PREV(b, s)(b->prev=(s))

#define SIZE(b)((size_t)BLK(b)->head.meta&~1L)
#define SET_SIZE(b, s)(((size_t *)&BLK(b)->head.meta)[0]=(s))
#define SET_SIZE_USE(b, s)SET_SIZE(b,s|1L);

#define IN_USE(b)((size_t)BLK(b)->head.meta&1L)
#define TOGGLE_USE(b)SET_SIZE(b, IN_USE(b)?SIZE(b):SIZE(b)|1L)

#define NEXT(b)(BLK(b)->pntr)
#define SET_NEXT(b,s) (((blk_t **)&BLK(b)->pntr)[0]=(s))
#endif

#define MEM(b)((void *)&BLK(b)->pntr)


#define ALIGN(n) (n + sizeof(void *) - 1) & ~(sizeof(void *) - 1)
#define META_RESIZE(n) (sizeof(struct head_) + (n))
#define CAN_SPLIT(b, sz) ((SIZE(b) - sz) >= MIN_SIZE)
#define GET_HEAD(b) ((blk_t *)(((char *)b) - sizeof(struct head_)))
#define GET_SUCCESSIVE_HEAD(b) BLK((char*)&BLK(b)->pntr + SIZE(b))
#define CAN_MERGE(b) ((list_index >= 0) && (GET_SUCCESSIVE_HEAD(b) != limit) && (!IN_USE(GET_SUCCESSIVE_HEAD(b))))
#define LIST_INDEX(sz) (sz / sizeof(void *) - 1)

#define FREELIST segregated_list[list_index]
#define FREETOP segregated_tops[list_index]
#define LAST_FIT segregated_last_fits[list_index]

#define FIRST_FIT 0
#define BEST_FIT 01
#define NEXT_FIT 0

#define TIMER clock()
#define TTIME(t) (((double)(clock() - t)) / CLOCKS_PER_SEC)

#ifdef DINFO
#define NWBLK(s) printf("getting new block [%zu]\n",(size_t)s);
#define NWLN putchar('\n');
#define ALLOCN(s) printf("alloc'n %zu\n",(size_t)s);
#define LINDXN printf("using index %i\n",list_index+1);
#define RUBLK(b) printf("reusing block(%p)[%zu]\n",b, (size_t)SIZE(b));
#define UBESTF printf("using BEST FIT\n");
#define BFAM(n,a,b,c)printf("blocks %i: %p %p %p\n",n, a,b,c);
#define SPLBK(a,b, sa,sb,os)printf("blk(%p)[%zu]=blk(%p)[%zu]+blk(%p)[%zu]\n", a, (size_t)os, a,(size_t)sa, b, (size_t)sb);
#define FBLK(b)printf("free block(%p)[%zu]\n", b, (size_t)SIZE(b));
#define MBLK(a, b)printf("merge blk(%p)[%zu] <-> blk(%p)[%zu]\n", a, (size_t)SIZE(a),b,(size_t)SIZE(b));
#define MBSTAT(a,b,c)printf("%zu+%zu=%zu\n", a, b, c);
#define FRED(a)printf("free'd blk(%p)[%zu]\n", a, (size_t)SIZE(a));
#else

#define NWBLK(s)
#define NWLN
#define ALLOCN(s)
#define LINDXN
#define RUBLK(b)
#define UBESTF
#define BFAM(n,a,b,c)
#define SPLBK(a,b, sa,sb,os)
#define FBLK(b)
#define MBLK(a, b)
#define MBSTAT(a,b,c)
#define FRED(a)
#endif
// blk_t *heap_start = 0;
// blk_t *top;

int list_index = -1;
size_t gen_size=17*sizeof(void*);

blk_t *segregated_list[17];
blk_t *segregated_tops[17];
blk_t *segregated_last_fits[17];

void *limit = 0;
void *cbrk=0, *cmbrk=0;
size_t heap_size=1024*1024*4;

static void afree(void *b);

static void *csbrk(size_t s){
    if(!cbrk){
        cbrk = mmap(0, heap_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1,0);
        cmbrk=cbrk+heap_size;
    }
    if(!s)return cbrk;
    void *ptr=cbrk+s;
    if(ptr>cmbrk)
    return (void *)-1;
    void *_=cbrk;
    cbrk=ptr;
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
static inline void *fix_free_list(blk_t *b,blk_t*p, blk_t*n)
{
    if(!IN_USE(b))TOGGLE_USE(b);
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
    if (!FREELIST)
        return 0;
        //blk_t *p=0;
    if (FIRST_FIT)
    {
        blk_t *b = FREELIST;
        while (b)
        {
            if (SIZE(b) >= size && !IN_USE(b))
            {
                // b->head.in_use = 1;
                return fix_free_list((blk_t *)b,PREV(b),NEXT(b));
            }
            //p=b;
            b = NEXT(b);
        }
    }
    else if (BEST_FIT)
    {
        UBESTF;
        blk_t *b = FREELIST,
                     *res = 0;
        while (b)
        {
            BFAM(0,PREV(b), b, NEXT(b));
            //BFAM(1,p, b, NEXT(b));
            if (SIZE(b) >= size && !IN_USE(b))
            {
                if (SIZE(b) == size)
                {
                    return fix_free_list((blk_t *)b,PREV(b),NEXT(b));
                }
                if (res ? SIZE(res) > SIZE(b) : 1)
                    res = b;
            }
            //p=b;
            b = NEXT(b);
            //printf("%p\n", b);
        }
        if (res)
        {
            return fix_free_list((blk_t *)res, PREV(res), NEXT(res));
        }
        return res;
    }
    else if (NEXT_FIT)
    {
        blk_t*b = (LAST_FIT ? LAST_FIT : FREELIST);
        while (b)
        {
            if (SIZE(b) >= size && !IN_USE(b))
            {
                LAST_FIT = NEXT(b);
                return fix_free_list((blk_t *)b,PREV(b),NEXT(b));
                
            }
            //p=b;
            b =NEXT(b) ? NEXT(b) : FREELIST;
            if ((b == (FREELIST) && LAST_FIT == 0) || ((blk_t *)b) == LAST_FIT)
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
    size_t osize;
    SET_SIZE_USE(bb, osize= SIZE(b)- META_RESIZE(sz));
    SET_SIZE_USE(b, sz);
    SPLBK(b,bb, sz, SIZE(bb), osize);
    afree(MEM(bb));
}
static inline void set_list_index(size_t sz)
{
    if(sz>=gen_size){
        list_index=16;
    }else{
    list_index = LIST_INDEX(sz);
    }
    LINDXN;
}
static void *alloc(size_t sz)
{
    NWLN;
    ALLOCN(sz);
    if (!sz)
        return 0;
    sz = ALIGN(sz);
#ifdef SMALL_META
if(sz<min_alloc)sz=min_alloc;
#endif
    ALLOCN(sz);
    if (list_index < 0)
    {
        memset(segregated_list, 0, sizeof(segregated_list));
        memset(segregated_tops, 0, sizeof(segregated_tops));
        memset(segregated_last_fits, 0, sizeof(segregated_last_fits));
    }
    set_list_index(sz);
    if(!FREELIST&&segregated_list[16])list_index=16;
    LINDXN;
    // printf("alloc %zu\n", sz);
    blk_t *b;
    if ((b = reuse_block(sz)))
    {
        RUBLK(b);
        if (CAN_SPLIT(b, sz))
            split(b, sz);
        return MEM(b);
    }
    NWBLK(sz);
    b = get_mem_chunk(META_RESIZE(sz));
    if (!b)
    {
        fwrite("OOM...\n", 1, 7, stderr);
        exit(-3);
    }
    //b->head.in_use = 1;
    SET_SIZE_USE(b, sz);
    NWLN;
    return MEM(b);
}
static void merge(blk_t *b)
{
    blk_t *nb = GET_SUCCESSIVE_HEAD(b);
    set_list_index(SIZE(nb));
    MBLK(b,nb);
    SET_NEXT(b,NEXT(nb));
    size_t _ =SIZE(b), k = META_RESIZE(SIZE(nb)),
    new_size=SIZE(b) + META_RESIZE(SIZE(nb));
    SET_SIZE(b, new_size);
    fix_free_list(nb, PREV(nb), NEXT(nb));
    MBSTAT(new_size, k,_);
    //adjust_free_list(nb, b, NEXT(nb));
    // printf("merge %zu, %zu+%zu\n", b->head.size, META_RESIZE(nb->head.size), _);
}
static void afree(void *b)
{
    if (!b)
        return;
    blk_t *h = GET_HEAD(b);
    FBLK(h);
    
    if (!IN_USE(h))
        return;
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
        if (!FREELIST)
        {
            FREELIST = h;
        }
        else
        {
            SET_NEXT(FREETOP,h);
        }

        SET_PREV(h,  FREETOP);
        //_ = PREV(h);
        FREETOP = h;
        SET_NEXT(h, 0);
    FRED(h);
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
    //exit(9);
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
