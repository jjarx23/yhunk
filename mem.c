#include <unistd.h>
#include <time.h>
#include <stdlib.h>
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

#define align(n) (n + sizeof(void *) - 1) & ~(sizeof(void *) - 1)
#define alloc_size(n) (sizeof(struct head_) + n)
#define FIRST_FIT 0
#define BEST_FIT 01
#define NEXT_FIT 1

#define TIMER clock()
#define TTIME(t) (((double)(clock()-t))/CLOCKS_PER_SEC)

blk_t *heap_start = 0;
blk_t *top;
blk_t *last_fit = 0;

void *get_mem_chunk(size_t sz)
{
    blk_t *brk = sbrk(0);
    if (sbrk(sz) == (void *)-1)
        return 0;
    return brk;
}
void *reuse_block(size_t size)
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
            b = b->next;
        }
    }
    else if (BEST_FIT)
    {
        struct head_ *b = (struct head_ *)heap_start,
        *res=0;
        while (b)
        {
            if (b->size >= size && !b->in_use)
            {
              if(b->size==size){
                b->in_use=1;
                //printf("exact \n");
                return b;
              }
              if(res?res->size>b->size:1)res=b;
            }
            b = b->next;
            //printf("no match\n");
        }
        if(res)res->in_use=1;
        //printf("%p\n", res);
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
                last_fit = b->next;//exit(0);
          //printf("h next[%p]{%zu}\n",last_fit, b->size);
                return b;
            }
     b = b->next?b->next:heap_start;
     //printf("%p->[%p]  %p::%p\n", b, b?b->next:0, last_fit, heap_start);
     if((b==heap_start&&last_fit==0)||b==last_fit){
       //printf("not found\n");
       return 0;
     }
        }
    }
    return 0;
}
void *alloc(size_t sz)
{
    sz = align(sz);
    blk_t *b;
    if ((b = reuse_block(sz)))
    {
        return &b->ptr;
    }
    b = get_mem_chunk(alloc_size(sz));
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
#define get_head(b) ((blk_t *)(((char *)b) - sizeof(struct head_)))
void afree(void *b)
{
    get_head(b)->head.in_use = 0;
}
int main()
{

    void *b = alloc(3);
    // printf("%i ; %zu\n", 3, get_head(b)->head.sz);

    clock_t t;

    t = TIMER;
    // work here
    int i = 100000;
    int j=0;
    void * _[i];
    while (i)
    {
        i--;
        _[j]=(malloc((100000-i)%83 + 3));
        if(i%13)
        free(_[j]);
        else{
          j++;
        }
    }
while(j){
  j--;
  free(_[j]);
}

    printf(" took %f seconds to execute \n", TTIME(t));

    t = TIMER;

    // work here
    i = 100000;
    j=0;
    while (i)
    {
        i--;
        _[j]=(alloc((100000-i)%83 + 3));
        if(i%13)
        afree(_[j]);
        else{
          j++;
        }
    }
while(j){
  j--;
  afree(_[j]);
}

    printf(" took %f seconds to execute \n", TTIME(t));
    return 0;
}
