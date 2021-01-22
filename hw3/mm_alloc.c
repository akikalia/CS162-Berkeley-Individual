/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines.
 */

#include "mm_alloc.h"
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

struct mdata{
    size_t size;
    char free;
    struct mdata *next;
};

static struct mdata * heap_start = NULL;

void split(struct mdata * block, size_t size){
    struct mdata *block2 = (struct mdata *)((char *)block + size);
    block2->size = block->size - sizeof(struct mdata) - size;
    block2->free = 1;
    block2->next = block->next;
    block->size = size;
    block->next = block2;  
}

struct mdata *find_block(size_t size){
    struct mdata *block = heap_start;
    while (block)
    {
        if (block->free && block->size >= size){
            if (block->size >= size + sizeof(struct mdata))
            {
                split(block, size);
            }
            return block;
        }
        block = block->next;
    }
    return block;
}


struct mdata *block_last(){
    struct mdata * cur = heap_start;
    while(cur->next){
        cur = cur->next;
    }
    return cur;
}

void *mm_malloc(size_t size) {
    /* YOUR CODE HERE */
    struct rlimit rlim;
    getrlimit(RLIMIT_DATA, &rlim );
    rlim_t lmt = rlim.rlim_max;
    if (!size || ((rlim_t)sbrk(0) + size + sizeof(struct mdata) >= lmt)) return NULL;
    // brk_loc = sbrk(0);
    struct mdata * block; 
    if ((block = find_block(size))){
        block->free = 0;
    }else{
        if (!(block = sbrk(size + sizeof(struct mdata))) || (rlim_t)block >= lmt)
            return NULL;
        block->free = 0;
        block->next = NULL;
        block->size = size;
        if (!heap_start)
            heap_start = block;
        else
            block_last()->next = block;
        
    }
    bzero(block + 1, block->size);
    return block+1;
}

void *mm_realloc(void *ptr, size_t size) {
    /* YOUR CODE HERE */
    struct mdata * curr_block = ptr;
    void * new_ptr;

    if (size == 0){
        mm_free(ptr);
        return NULL;
    }
    if (ptr == NULL){
        return  mm_malloc(size);
    }
    curr_block--;
    if (curr_block->size >= size){
        return ptr;
    }else{
        new_ptr = mm_malloc(size);
        if (!new_ptr)
            return NULL;
        memcpy(new_ptr, ptr, curr_block->size);
    }
    return new_ptr;
}

void chain(){
    struct mdata *cur;
    struct mdata *next;

    cur = heap_start;
    next = cur->next;
    while(next){
        if(cur->free && next->free){
            cur->size += next->size + sizeof(struct mdata);
            cur->next = next->next;
            next = cur->next;
        }
        else{
            cur = cur->next;
            if (cur)
                next = cur->next;
            else
                next = NULL;
        }
    }
}

void mm_free(void *ptr) {
    struct mdata *block = ptr;
    if (!ptr)
        return;
    block--;
    block->free = 1;
    chain();
}
