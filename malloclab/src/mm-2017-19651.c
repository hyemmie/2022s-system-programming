/*
 * 2017-19651 
 * mm.c - implemented with segregated list and best fit strategy
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE   4               /* Word and header/footer size (bytes) */
#define DSIZE   8               /* Double word size (bytes) */
#define CHUNKSIZE (1<<6)        /* Extend heap by this amount (bytes) */
#define DCHUNKSIZE (1<<12)
#define SEG_SIZE 24             /* Maximum segregated list count */
#define MAX(x, y) ((x) > (y)?  (x) : (y))
#define MINSIZE 200             /* Size limitation for efficiency */

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)      (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address */
#define GET_SIZE(p)     (GET(p) & ~0x7)
#define GET_ALLOC(p)    (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)    ((char *)(bp) - WSIZE)
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Helper macros for segregated list */
#define GET_VAL(ptr)        ((char **)(ptr))
#define UNSIGN(p)           ((unsigned int)(p))
#define GET_BLK_SIZE(ptr)   (GET_SIZE(HDRP(ptr)))
#define SET_SEG_LIST_PTR(ptr, idx, val)  (*(GET_VAL(ptr) + idx) = val)
#define GET_SEG_LIST_PTR(ptr, idx) (*(GET_VAL(ptr) + idx))
#define GET_SEG_PREV_ADR(bp)    ((char *)(bp))
#define GET_SEG_NEXT_ADR(bp)    ((char *)(bp) + WSIZE)
#define SEG_PREV_BLKP(bp)    (*(GET_VAL(bp)))
#define SEG_NEXT_BLKP(bp)    (*(GET_VAL(GET_SEG_NEXT_ADR(bp))))

/* Global pointer */
static char *heap_ptr = 0;          /* pointer for heap */
static void *seg_list_ptr;          /* pointer for segregated list*/

/* Helper functions */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize, size_t csize, int index);
static void *place(void *bp, size_t asize);
static void init_seglist(void);
static void add_seglist(void *bp, size_t blk_size);
static void remove_seglist(void *bp);
static int mm_check(void);

/* 
 * mm_init - initialize the malloc package.
 * referenced the textbook
 */
int mm_init(void)
{
    init_seglist();
    
    /* Create the initial empty heap */
    if ((heap_ptr = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
        
    PUT(heap_ptr, 0);                             /* Alignment padding */
    PUT(heap_ptr + (1*WSIZE), PACK(DSIZE, 1));    /* Prologue header */
    PUT(heap_ptr + (2*WSIZE), PACK(DSIZE, 1));    /* Prologure header */
    PUT(heap_ptr + (3*WSIZE), PACK(0, 1));        /* Epilogue header */
    heap_ptr += (2*WSIZE);
    
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 * referenced the textbook
 */
void *mm_malloc(size_t size)
{
    size_t asize;            /* Adjusted block size */
    size_t extendsize;       /* Amount to extend heap if no fit */
    char *bp;

    /* Ignore spurious requests */
    if (size <= 0)
        return NULL;
       
    /* Adjust block size to include overhead and alignment reqs */
    if (size <= 2*DSIZE)
         asize = 3*DSIZE;
    else
         asize = ALIGN(size + DSIZE);

    /* Search the free list for a fit */
    if ((bp = find_fit(asize, asize, 0)) != NULL) {
        return place(bp, asize);
    }
    
    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, DCHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;

    return place(bp, asize);
}

/*
 * mm_free - Freeing a block does nothing.
 * then 
 *  1) insert into the segregated list
 *  2) coalesce
 * referenced the textbook
 */
void mm_free(void *bp)
{
    size_t size = GET_BLK_SIZE(bp);

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    
    add_seglist(bp, size);
    coalesce(bp);

    return;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *old_ptr = ptr;
    void *new_ptr = NULL;
    int realloced = 0;

    /* initial allocation */
    if (ptr == NULL)
        return mm_malloc(size);

    /* if new size is zero, free */
    if (size == 0) {
        mm_free(ptr);
        return ptr;
    }
    
    size_t csize = GET_BLK_SIZE(ptr) - DSIZE;   /* Block size without header and footer */
    size_t asize = ALIGN(size);                 /* Adjusted block size */

    /* realloc size == old size */
    if (asize == csize)
        return old_ptr;

    size_t padding = 0;
    /* realloc size < old size */
    if (asize < csize) {
        padding = csize - asize;
        if (padding <= DSIZE) {
            return old_ptr;
        }
        PUT(HDRP(old_ptr), PACK(asize + DSIZE, 1));
        PUT(FTRP(old_ptr), PACK(asize + DSIZE, 1));
        new_ptr = old_ptr;
        realloced = 1;
    }
    /* realloc size > old size */
    else if (asize > csize) {
        void *next_ptr = NULL;
        void *prev_ptr = NULL;
        
        next_ptr = NEXT_BLKP(old_ptr);
        prev_ptr = PREV_BLKP(old_ptr);
        size_t nsize, psize;
        size_t new_size = 0;
        
        nsize = (next_ptr) ? GET_BLK_SIZE(next_ptr) : 0;
        psize = (prev_ptr) ? GET_BLK_SIZE(prev_ptr) : 0;

        int previous_merge = prev_ptr != NULL && !GET_ALLOC(HDRP(prev_ptr));
        int next_merge = next_ptr != NULL && !GET_ALLOC(HDRP(next_ptr));        

        if (previous_merge) {
            if (psize + csize >= asize) {
                padding = csize + psize - asize;
                remove_seglist(prev_ptr);
                new_size = (padding > DSIZE) ? asize + DSIZE : csize + psize + DSIZE;
                new_ptr = prev_ptr;
                PUT(HDRP(new_ptr), PACK(new_size, 1));
                memmove(new_ptr, old_ptr, csize + DSIZE);
                PUT(FTRP(new_ptr), PACK(new_size, 1));
                realloced = 1;
            }
        } else if (next_merge) {
            if (nsize + csize >= asize) {
                padding = csize + nsize - asize;
                remove_seglist(next_ptr);
                new_size = (padding > DSIZE) ? asize + DSIZE : csize + nsize + DSIZE;
                PUT(HDRP(old_ptr), PACK(new_size, 1));
                PUT(FTRP(old_ptr), PACK(new_size, 1));
                new_ptr = old_ptr;
                realloced = 1;
            }
        }

        int previous_next_merge = !GET_ALLOC(HDRP(prev_ptr)) && !GET_ALLOC(HDRP(next_ptr));

        if (previous_next_merge) {
            if (psize + nsize + csize >= asize) {
                padding = csize + psize + nsize - asize;
                remove_seglist(next_ptr);
                remove_seglist(prev_ptr);
                new_size = (padding > DSIZE) ? asize + DSIZE : csize + psize + nsize + DSIZE;
                new_ptr = prev_ptr;
                PUT(HDRP(new_ptr), PACK(new_size, 1));
                memmove(new_ptr, old_ptr, csize+DSIZE);
                PUT(FTRP(new_ptr), PACK(new_size, 1));
                realloced = 1;
            }
        }
    }

    if (realloced) {
        void * ret = NULL;
        if (padding > DSIZE) {
            ret = NEXT_BLKP(new_ptr);
            PUT(HDRP(ret), PACK(padding, 0));
            PUT(FTRP(ret), PACK(padding, 0));
            add_seglist(ret, GET_BLK_SIZE(ret));
            coalesce(ret);
        }
    } else {
        new_ptr = mm_malloc(size);
        if (new_ptr == NULL)
            return NULL;
        memcpy(new_ptr, old_ptr, csize + DSIZE);
        mm_free(old_ptr);
    }
    
    return new_ptr;
}

/*
 * extend_heap - extend heap memory and manage new free block
 * referenced the textbook
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    
    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1)*WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    
    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));           /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));           /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   /* New epilogue header */
    
    /* insert free block to segregated list */
    add_seglist(bp, size);

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}


/*
 * coalesce - merge adjacent free blocks
 * referenced the textbook
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* Case 1 */
    if (prev_alloc && next_alloc) {
        return bp;
    }
    
    /* remove current free block from segregated list */
    remove_seglist(bp);

    /* Case 2 */
    if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_seglist(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    /* Case 3 */
    else if (!prev_alloc && next_alloc) {
        remove_seglist(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    /* Case 4 */
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        remove_seglist(PREV_BLKP(bp));
        remove_seglist(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    /* add merged free block to segregated list */
    add_seglist(bp, size);
    return bp;
}

/*
 * find_fit - implement best-fit strategy
 */
static void *find_fit(size_t asize, size_t csize, int index)
{
   
    void *seg_ptr = NULL;

    if (index > SEG_SIZE)
        return seg_ptr;

    seg_ptr = GET_SEG_LIST_PTR(seg_list_ptr, index);
    
    /* search best fit */
    if (csize <= 1 && seg_ptr != NULL) {
        while (seg_ptr != NULL && (asize > GET_BLK_SIZE(seg_ptr)))
            seg_ptr = SEG_PREV_BLKP(seg_ptr);
        if (seg_ptr != NULL)
            return seg_ptr;
    }
    csize >>= 1;

    /* if not found */
    return find_fit(asize, csize, index + 1);
}

/*
 * place - returns a pointer at best-fit position
 */
static void *place(void *bp, size_t asize)
{
    size_t csize = GET_BLK_SIZE(bp);
    size_t padding = csize - asize;
    void *nbp = NULL;
    remove_seglist(bp);
    
    /* if the remaining is not enough */
    if (padding < 2 * DSIZE) {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        return bp;
    } else if (asize > MINSIZE) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        nbp = NEXT_BLKP(bp);
        PUT(HDRP(nbp), PACK(padding, 0));
        PUT(FTRP(nbp), PACK(padding, 0));
        add_seglist(nbp, padding);
        return bp;
    } else {
        PUT(HDRP(bp), PACK(padding, 0));
        PUT(FTRP(bp), PACK(padding, 0));
        nbp = NEXT_BLKP(bp);
        PUT(HDRP(nbp), PACK(asize, 1));
        PUT(FTRP(nbp), PACK(asize, 1));
        add_seglist(bp, padding);
        return nbp;
    }
}

/*
 * init_seglist - initialize segregated lists
 */
static void init_seglist(void) 
{
    /* make space for the segregated lists */
    seg_list_ptr = mem_sbrk(SEG_SIZE * WSIZE);
    
    /* initialize the segregated lists */
    for (int i = 0; i < SEG_SIZE; i++) {
        SET_SEG_LIST_PTR(seg_list_ptr, i, NULL);
    }
}

/*
 * add_seglist - add new free block to segregated list
 */
static void add_seglist(void *bp, size_t blk_size)
{
    void *curr = NULL;
    void *prev = NULL;
    int index;
  
    /* find the offset of segregated list */
    for (index = 0; index < SEG_SIZE - 1; index++) {
        if (blk_size <= 1)
            break;
        blk_size >>= 1;
    }
    
    /* the blk_size's class address of segregated list */
    curr = GET_SEG_LIST_PTR(seg_list_ptr, index);
    
    while (curr != NULL && blk_size > GET_BLK_SIZE(curr)) {
        prev = curr;
        curr = SEG_PREV_BLKP(curr);
    }
    
    if (curr && prev) {
        PUT(GET_SEG_PREV_ADR(prev), UNSIGN(bp));
        PUT(GET_SEG_NEXT_ADR(bp), UNSIGN(prev));
        PUT(GET_SEG_PREV_ADR(bp),UNSIGN(curr));
        PUT(GET_SEG_NEXT_ADR(curr), UNSIGN(bp));
    } else if (curr && !prev) {
        PUT(GET_SEG_NEXT_ADR(curr), UNSIGN(bp));
        PUT(GET_SEG_PREV_ADR(bp), UNSIGN(curr));
        PUT(GET_SEG_NEXT_ADR(bp), UNSIGN(NULL));
        SET_SEG_LIST_PTR(seg_list_ptr, index, bp);
    } else if (!curr && prev) {
        PUT(GET_SEG_NEXT_ADR(bp), UNSIGN(prev));
        PUT(GET_SEG_PREV_ADR(prev), UNSIGN(bp));
        PUT(GET_SEG_PREV_ADR(bp), UNSIGN(NULL));
    } else {
        PUT(GET_SEG_NEXT_ADR(bp), UNSIGN(NULL));
        PUT(GET_SEG_PREV_ADR(bp), UNSIGN(NULL));
        SET_SEG_LIST_PTR(seg_list_ptr, index, bp);
    }
}
    
/*
 * remove_seglist - remove new allocated block from segregated list
 */
static void remove_seglist(void *bp)
{
    void *prev = SEG_PREV_BLKP(bp);
    void *next = SEG_NEXT_BLKP(bp);
    size_t size = GET_BLK_SIZE(bp);
    void *list_ptr = NULL;
    
    int index;
    /* find the offset of segregated list */
    for (index = 0; index < SEG_SIZE - 1; index++) {
       if (size <= 1)
          break;
        
       size >>= 1;
    }
    
    /* removing the block depends on the next and prev */
    if (next == NULL) {
        SET_SEG_LIST_PTR(seg_list_ptr, index, prev);
        list_ptr = GET_SEG_LIST_PTR(seg_list_ptr, index);
        if (list_ptr != NULL)
            PUT(GET_SEG_NEXT_ADR(list_ptr), UNSIGN(NULL));
    } else {
        PUT(GET_SEG_PREV_ADR(next), UNSIGN(prev));
        if (prev != NULL)
            PUT(GET_SEG_NEXT_ADR(prev), UNSIGN(next));
    }
}

/*
 * mm_check - check segregated list and heap
 */
static int mm_check(void) {
    int errno = 0;

    void *curr = NULL;
    curr = heap_ptr;

    /* heap valid check */
    while (curr != NULL && GET_SIZE(HDRP(curr)) != 0) {
        /* header and footer consistency check */
        if (GET_ALLOC(HDRP(curr)) != GET_ALLOC(FTRP(curr))) {
            printf("BLOCK %p HEADER AND FOOTER HAS DIFFERENT ALLOCATION BIT\n", curr);
            errno = -1;
        }
        if (GET_SIZE(HDRP(curr)) != GET_SIZE(FTRP(curr))) {
            printf("BLOCK %p HEADER AND FOOTER HAS DIFFERENT SIZE\n", curr);
            errno = -1;
        }
        /* block address check */
        if (curr < mem_heap_lo() || curr > mem_heap_hi()) {
            printf("BLOCK %p INVALID\n", curr);
        }
        curr = NEXT_BLKP(curr);
    }

    void * blkp = NULL;
    void * nblkp = NULL;
    
    /* segregated list valid check */
    for (int i = 0; i < SEG_SIZE; i++) {
        blkp = GET_SEG_LIST_PTR(seg_list_ptr, i);

        while (blkp != NULL) {
            /* block address check */
            if (blkp < mem_heap_lo() || blkp > mem_heap_hi()) {
                printf("FREE BLOCK %p INVALID\n", blkp);
                errno = -1;
            }
            /* allocated block is in free list check */
            if (GET_ALLOC(blkp)) {
                printf("FREE BLOCK %p MARKED ALLOC\n", blkp);
                errno = -1;
            }
            /* header and footer consistency check */
            if (GET_SIZE(HDRP(blkp)) != GET_SIZE(FTRP(blkp))) {
                printf("FREE BLOCK %p HEADER AND FOOTER HAS DIFFERENT SIZE\n", blkp);
                errno = -1;
            }
            /* alignment rule check */
            if (UNSIGN(blkp) % DSIZE != 0) {
                printf("FREE BLOCK %p SHOULD BE 8 BYTE ALIGNED\n", blkp);
                errno = -1;
            }
            nblkp = SEG_PREV_BLKP(blkp);
            /* appropriate coalesce check*/
            if (nblkp != NULL && HDRP(blkp) - FTRP(blkp) == DSIZE) {
                printf("FREE BLOCK %p SHOULD BE COALESCED\n", blkp);
                errno = -1;
            }
            blkp = nblkp;
        }
    }    
    return errno;
}
