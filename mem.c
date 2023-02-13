#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
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
#define CAN_MERGE(b) (GET_SUCCESSIVE_HEAD(b) == ((blk_t *)b)->head.next && ((blk_t *)b)->head.next->head.in_use == 0)

#define FIRST_FIT 0
#define BEST_FIT 01
#define NEXT_FIT 0

#define TIMER clock()
#define TTIME(t) (((double)(clock() - t)) / CLOCKS_PER_SEC)

blk_t *heap_start = 0;
blk_t *top;
blk_t *last_fit = 0;

static void *get_mem_chunk(size_t sz)
{
    blk_t *brk = sbrk(0);
    if (sbrk(sz) == (void *)-1)
        return 0;
    return brk;
}
static void *reuse_block(size_t size)
{
    if (FIRST_FIT)
    {
        struct head_ *b = (struct head_ *)heap_start;
        while (b)
        {
            if (b->size >= size && !b->in_use)
            {
                b->in_use = 1;
                return b;
            }
            b = (struct head_ *)b->next;
        }
    }
    else if (BEST_FIT)
    {
        struct head_ *b = (struct head_ *)heap_start,
                     *res = 0;
        while (b)
        {
            if (b->size >= size && !b->in_use)
            {
                if (b->size == size)
                {
                    b->in_use = 1;
                    return b;
                }
                if (res ? res->size > b->size : 1)
                    res = b;
            }
            b = (struct head_ *)b->next;
        }
        if (res)
            res->in_use = 1;
        return res;
    }
    else if (NEXT_FIT)
    {
        struct head_ *b = (struct head_ *)(last_fit ? last_fit : heap_start);
        while (b)
        {
            if (b->size >= size && !b->in_use)
            {
                b->in_use = 1;
                last_fit = b->next;
                return b;
            }
            b = (struct head_ *)(b->next ? b->next : heap_start);
            if ((b == ((struct head_ *)heap_start) && last_fit == 0) || ((blk_t *)b) == last_fit)
            {
                return 0;
            }
        }
    }
    return 0;
}
static blk_t *split(blk_t *b, size_t sz)
{
    blk_t *bb = (blk_t *)(((char *)(b)) + META_RESIZE(sz));
    bb->head.size = b->head.size - META_RESIZE(sz);
    b->head.size = sz;
    bb->head.next = b->head.next;
    b->head.next = bb;
    bb->head.in_use = 0;
    return bb;
}
static void *alloc(size_t sz)
{
    sz = ALIGN(sz);
    blk_t *b;
    if ((b = reuse_block(sz)))
    {
        if (CAN_SPLIT(b, sz))
            split(b, sz);
        return &b->ptr;
    }
    b = get_mem_chunk(META_RESIZE(sz));
    b->head.in_use = 1;
    b->head.size = sz;
    if (!heap_start)
        heap_start = top = b;
    else
        top->head.next = b;
    top = b;
    b->head.next = 0;
    return &b->ptr;
}
static void merge(blk_t *b)
{
    blk_t *nb = GET_SUCCESSIVE_HEAD(b);
    b->head.next = nb->head.next;
    b->head.size = META_RESIZE(nb->head.size);
}
static void afree(void *b)
{
    blk_t *h = GET_HEAD(b);
    h->head.in_use = 0;
    if (CAN_MERGE(h))
        merge(h);
}
int main()
{

    void *b = alloc(3);
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
