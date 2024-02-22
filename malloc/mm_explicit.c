#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* Basic constatnt and macros */
#define WSIZE		4		/* 워드 / 헤드 풋터 사이즈 (bytes) */
#define DSIZE		8		/* 더블 워드 사이즈 (Bytes) */
#define CHUNKSIZE	(1<<12)	/* 힙 확장하는 사이즈 (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))	// 크기 비교하는 함수

#define PACK(size, alloc)	((size) | (alloc))

#define GET(p)		        (*(unsigned int*)(p))	
#define PUT(p, val)		    (*(unsigned int*)(p) = (val))

#define GET_SIZE(p)		    (GET(p) & ~0x7)
#define GET_ALLOC(p)	    (GET(p) & 0x1)

#define HDRP(bp)		    ((char*)(bp) - WSIZE)
#define FTRP(bp)		    ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp)	    ((char*)(bp) + GET_SIZE((HDRP(bp))))
#define PREV_BLKP(bp)	    ((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))

static void place(void *bp, size_t asize);
static void* extend_heap(size_t words);
static char *heap_listp = 0;  /* Points to the start of the heap */

team_t team = {
    /* Team name */
    " 3211_1st",
    /* First member's full name */
    " Dongjun Kim ",
    /* First member's email address */
    " wbs4808@gmail.com",
    /* Second member's full name (leave blank if none) */
    " SungKyul Choo ",
    /* Second member's email address (leave blank if none) */
    " Subin Kim "
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 
 * mm_init - initialize the malloc package.
 */

int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void*)-1) {
        return -1;
    }
    
    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // 프롤로그
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // 프롤로그
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1)); // 에필로그
    
    heap_listp += (2 * WSIZE);
    
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize = 0;
    size_t esize = 0;
    char* bp;

    if (size == 0) {
        return NULL;
    } else if (size <= DSIZE) {
        asize = 2 * DSIZE;
    } else {
        asize = (((size + DSIZE + (DSIZE - 1)) / DSIZE) * DSIZE);
    }

	/* Search the free list for a fit */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	/* No fit found. Get more memory and place the block */
	esize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(esize/WSIZE)) == NULL)
		return NULL;

	place(bp, asize);
	return bp;
    
}

static void* coalesce(void* bp) {
	
    size_t pre_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
   
    // 1. 앞뒤 가용 없음
    if (pre_alloc && next_alloc) {
        return bp;
    }
    // 2. 뒤에 가용 없음
    else if (!pre_alloc && next_alloc) {
        PUT(HDRP(PREV_BLKP(bp)), PACK(GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(bp)), 0));
        PUT(FTRP(bp), PACK(GET_SIZE(HDRP(PREV_BLKP(bp))), 0));
        bp = PREV_BLKP(bp);
    }
    // 3. 앞에 가용 없음
    else if (pre_alloc && !next_alloc) {
        PUT(HDRP(bp), PACK( (GET_SIZE(HDRP(bp))) + (GET_SIZE(HDRP(NEXT_BLKP(bp)))) , 0));
        PUT(FTRP(bp), PACK( (GET_SIZE(HDRP(bp))), 0 ));
    }
    // 4. 앞뒤 가용 있음
    else {
        PUT(HDRP(PREV_BLKP(bp)),
        PACK( GET_SIZE(HDRP(PREV_BLKP(bp)))
        + GET_SIZE(HDRP(bp))
        + GET_SIZE(FTRP(NEXT_BLKP(bp))) , 0));
        
        PUT(FTRP(NEXT_BLKP(bp)), PACK( GET_SIZE(HDRP(PREV_BLKP(bp))), 0));

        bp = PREV_BLKP(bp);
    }

    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void* bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
// 할당된 블록의 크기를 변경한다
void *mm_realloc(void *oldptr, size_t size)
{
    void *newptr;

    newptr = mm_malloc(size);

    int copysize = GET_SIZE(HDRP(oldptr)) - DSIZE;

    // 원래 있던 기존의 페이로드 크기가
    // 새로 할당할 페이로드 크기보다 크다면
    if (copysize > size) {
        // 새로 할당할 페이로드 크기를
        // 기존 페이로드 크기로 만듦
        copysize = size;
    }

    memcpy(newptr, oldptr, copysize);
    mm_free(oldptr);

    return newptr;
}

static void* extend_heap(size_t words)
{
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : (words * WSIZE);
    char* bp = mem_sbrk(words * WSIZE);

    if (bp == (void *)-1){
        return NULL;
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(FTRP(bp) + WSIZE, PACK(0, 1));
    
    return coalesce(bp);
}

static void *find_fit(size_t asize)
{
	char* bp;
    
    for (bp = heap_listp ; GET_SIZE(HDRP(bp)) > 0 ; bp = NEXT_BLKP(bp) )
    {
        if ( (asize <= GET_SIZE(HDRP(bp))) && (!GET_ALLOC(HDRP(bp)))){
            return bp;
        }
    }
    return NULL;
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    if ((csize - asize) >= (DSIZE * 2)){
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    } else 
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}