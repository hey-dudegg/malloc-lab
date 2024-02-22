
/* 메인 아이디어는 가용 리스트는 맨 앞에 위치할 수록 효율적이라는 점입니다.
이를 활용하여 가용 리스트가 만들어질 때 마다 연결 리스트의 맨 앞으로 위치를 정해줍니다. */

/*

Team Name: 3211_1st
Submitted : Dongjun Kim : wbs4808@gmail.com
Team Crew : SungKyul Choo & Subin Kim 
Using default tracefiles in ./traces/
Perf index = 42 (util) + 40 (thru) = 82/100
명시적 가용 리스트 구현 완료
findfit은 first fit /ll 순서는 free order로 구현

*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>

#include "mm.h"
#include "memlib.h"

/* -----------------------묵시적 가용리스트의 부분 -----------------------*/
#define WSIZE 4             /* 워드 & 헤드 풋터 사이즈 (bytes) */
#define DSIZE 8             /* 더블 워드 사이즈 (Bytes) */
#define CHUNKSIZE (1 << 12) /* 힙 확장하는 사이즈 (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y)) // 크기 비교하는 함수
#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((HDRP(bp))))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))

static void place(void *bp, size_t asize);
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
// static char *heap_listp = 0;  // heap 출발을 가르키는 포인터

/* -----------------------명시적 가용리스트의 부분 -----------------------*/
// 이전(PREV) 가용리스트의 bp 주소를 변경하기 위한 함수 == *(GET(PREV_FREEP(bp)))
#define PREV_FREEP(bp) (*(void **)(bp))
// 다음(NEXT) 가용리스트의 bp 주소를 변경하기 위한 함수 == *(GET(NEXT_FREEP(bp)))
#define NEXT_FREEP(bp) (*(void **)(bp + WSIZE))

void un_connect(void *bp);      // 가용 링크드 리스트 내 앞,뒤 연결을 끊는 매크로 함수
void connect(void *bp);         // 가용 링크드 리스트 내 앞,뒤를 연결해주는 함수
static char *heap_listp = NULL; // heap 출발을 가르키는 포인터
static char *free_listp = NULL; // 가용리스트의 첫번째(맨 앞) 블록을 가리키는 포인터

team_t team = {
    " 3211_1st",          /* Team name */
    " Dongjun Kim ",      /* First member's full name */
    " wbs4808@gmail.com", /* First member's email address */
    " SungKyul Choo ",    /* Second member's full name (leave blank if none) */
    " Subin Kim ", };       /* Second member's email address (leave blank if none) */

#define ALIGNMENT 8                                     /* 최소 할당 크기, DSIZE */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) /* 가장 근접한 최소 할당 크기를 반환해주는 매크로 함수 */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// malloc 초기 설정 함수
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(24)) == (void *)-1)
    {
        return -1;
    }

    PUT(heap_listp, 0);                       // 패딩
    PUT(heap_listp + WSIZE, PACK(16, 1));     // 프롤로그 헤더
    PUT(heap_listp + 2 * WSIZE, NULL);        // 프롤로그 PREC 포인터 NULL로 초기화
    PUT(heap_listp + 3 * WSIZE, NULL);        // 프롤로그 SUCC 포인터 NULL로 초기화
    PUT(heap_listp + 4 * WSIZE, PACK(16, 1)); // 프롤로그 풋터
    PUT(heap_listp + 5 * WSIZE, PACK(0, 1));  // 에필로그 헤더

    free_listp = heap_listp + DSIZE; // free_listp 초기화
    // 가용리스트에 블록이 추가될 때 마다 항상 리스트의 제일 앞에 추가될 것이므로
    // 지금 생성한 프롤로그 블록은 항상 가용리스트의 끝에 위치하게 된다.
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
    {
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
    void *bp;
    size_t extend_size;
    size_t asize;

    if (size == 0){
        return NULL;
    }
        

    if (size <= DSIZE)
    {
        asize = 2 * DSIZE;
    }
    else
    {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    }

    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize);
        return bp;
    }

    extend_size = MAX(asize, CHUNKSIZE);
    bp = extend_heap(extend_size / WSIZE);

    if (bp == NULL)
        return NULL;
    place(bp, asize);
    return bp;
    
}

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // 이미 가용리스트에 존재하던 블록은 연결하기 이전에 가용리스트에서 제거해준다.
    if (prev_alloc && !next_alloc)
    {
        un_connect(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc)
    {
        un_connect(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && !next_alloc)
    {
        un_connect(PREV_BLKP(bp));
        un_connect(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))) + GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    // 연결된 블록을 가용리스트에 추가
    connect(bp);
    return bp;
}
/*
static void *coalesce(void *bp)
{

    size_t pre_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

    // 1. 앞뒤 가용 없음
    if (pre_alloc && next_alloc)
    {
        return bp;
    }
    // 2. 뒤에 가용 없음
    else if (!pre_alloc && next_alloc)
    {
        PUT(HDRP(PREV_BLKP(bp)), PACK(GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(bp)), 0));
        PUT(FTRP(bp), PACK(GET_SIZE(HDRP(PREV_BLKP(bp))), 0));
        bp = PREV_BLKP(bp);
    }
    // 3. 앞에 가용 없음
    else if (pre_alloc && !next_alloc)
    {
        PUT(HDRP(bp), PACK((GET_SIZE(HDRP(bp))) + (GET_SIZE(HDRP(NEXT_BLKP(bp)))), 0));
        PUT(FTRP(bp), PACK((GET_SIZE(HDRP(bp))), 0));
    }
    // 4. 앞뒤 가용 있음
    else
    {
        PUT(HDRP(PREV_BLKP(bp)),
            PACK(GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(bp)) + GET_SIZE(FTRP(NEXT_BLKP(bp))), 0));

        PUT(FTRP(NEXT_BLKP(bp)), PACK(GET_SIZE(HDRP(PREV_BLKP(bp))), 0));

        bp = PREV_BLKP(bp);
    }

    return bp;
}
*/

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    coalesce(bp);
}

/*
void *mm_realloc(void *oldptr, size_t size)
{
    void *newptr;

    newptr = mm_malloc(size);

    int copysize = GET_SIZE(HDRP(oldptr)) - DSIZE;

    // 원래 있던 기존의 페이로드 크기가
    // 새로 할당할 페이로드 크기보다 크다면
    if (copysize > size)
    {
        // 새로 할당할 페이로드 크기를
        // 기존 페이로드 크기로 만듦
        copysize = size;
    }

    memcpy(newptr, oldptr, copysize);
    mm_free(oldptr);

    return newptr;
}
*/
/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
// 할당된 블록의 크기를 변경한다

void *mm_realloc(void *bp, size_t size)
{
    if (size <= 0)
    {
        mm_free(bp);
        return 0;
    }
    if (bp == NULL)
    {
        return mm_malloc(size);
    }
    void *newp = mm_malloc(size);
    if (newp == NULL)
    {
        return 0;
    }
    size_t oldsize = GET_SIZE(HDRP(bp));
    if (size < oldsize)
    {
        oldsize = size;
    }
    memcpy(newp, bp, oldsize);
    mm_free(bp);
    return newp;
}

static void *extend_heap(size_t words)
{
    size_t size;
    char *bp;
    size = words * WSIZE; // 물어보기
    if ((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    return coalesce(bp);
}

static void *find_fit(size_t asize)
{
    // first fit
    void *bp;
    bp = free_listp;
    // 가용리스트 내부의 유일한 할당 블록은 맨 뒤의 프롤로그 블록이므로 할당 블록을 만나면 for문을 종료한다.
    for (bp; GET_ALLOC(HDRP(bp)) != 1; bp = NEXT_FREEP(bp))
    {
        if (GET_SIZE(HDRP(bp)) >= asize)
        {
            return bp;
        }
    }
    return NULL;
}
/*
static void *find_fit(size_t asize)
{
    char *bp;

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        if ((asize <= GET_SIZE(HDRP(bp))) && (!GET_ALLOC(HDRP(bp))))
        {
            return bp;
        }
    }
    return NULL;
}
*/

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    // 할당될 블록이니 가용리스트 내부에서 제거해준다.
    un_connect(bp);
    if (csize - asize >= 16)
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        // 가용리스트 첫번째에 분할된 블럭을 넣는다.
        connect(bp);
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

// 새로 반환되거나 생성된 가용 블록을 가용리스트 맨 앞에 위치시키는 함수
void connect(void *bp)
{
    NEXT_FREEP(bp) = free_listp; // 현재 블럭의 NEXT 주소를 이전 f_p 값으로 저장
    PREV_FREEP(bp) = NULL;       // 맨 앞이므로 이전 블럭의 주소를 NULL;
    PREV_FREEP(free_listp) = bp;
    free_listp = bp;
}

// 해당 블록의 연결을 끊고 앞, 뒤의 블록들을 연결시키는 함수
void un_connect(void *bp)
{
    // 첫번째 블럭을 없앨 때
    if (bp == free_listp)
    {
        // 다음에 있는 블럭의 이전 블럭 포인터만 NULL처리하면 된다.
        PREV_FREEP(NEXT_FREEP(bp)) = NULL;
        free_listp = NEXT_FREEP(bp);
    }
    // 앞 뒤 모두 있을 때
    else
    {
        NEXT_FREEP(PREV_FREEP(bp)) = NEXT_FREEP(bp);
        PREV_FREEP(NEXT_FREEP(bp)) = PREV_FREEP(bp);
    }
}
