/*
 * Win32 heap functions
 *
 * Copyright 1996 Alexandre Julliard
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "selectors.h"
#include "winbase.h"
#include "winerror.h"
#include "winnt.h"
#include "heap.h"
#include "debug.h"

/* Note: the heap data structures are based on what Pietrek describes in his
 * book 'Windows 95 System Programming Secrets'. The layout is not exactly
 * the same, but could be easily adapted if it turns out some programs
 * require it.
 */

typedef struct tagARENA_INUSE
{
    DWORD  size;                    /* Block size; must be the first field */
    WORD   threadId;                /* Allocating thread id */
    WORD   magic;                   /* Magic number */
    DWORD  callerEIP;               /* EIP of caller upon allocation */
} ARENA_INUSE;

typedef struct tagARENA_FREE
{
    DWORD                 size;     /* Block size; must be the first field */
    WORD                  threadId; /* Freeing thread id */
    WORD                  magic;    /* Magic number */
    struct tagARENA_FREE *next;     /* Next free arena */
    struct tagARENA_FREE *prev;     /* Prev free arena */
} ARENA_FREE;

#define ARENA_FLAG_FREE        0x00000001  /* flags OR'ed with arena size */
#define ARENA_FLAG_PREV_FREE   0x00000002
#define ARENA_SIZE_MASK        0xfffffffc
#define ARENA_INUSE_MAGIC      0x4842      /* Value for arena 'magic' field */
#define ARENA_FREE_MAGIC       0x4846      /* Value for arena 'magic' field */

#define ARENA_INUSE_FILLER     0x55
#define ARENA_FREE_FILLER      0xaa

#define HEAP_NB_FREE_LISTS   4   /* Number of free lists */

/* Max size of the blocks on the free lists */
static const DWORD HEAP_freeListSizes[HEAP_NB_FREE_LISTS] =
{
    0x20, 0x80, 0x200, 0xffffffff
};

typedef struct
{
    DWORD       size;
    ARENA_FREE  arena;
} FREE_LIST_ENTRY;

struct tagHEAP;

typedef struct tagSUBHEAP
{
    DWORD               size;       /* Size of the whole sub-heap */
    DWORD               commitSize; /* Committed size of the sub-heap */
    DWORD               headerSize; /* Size of the heap header */
    struct tagSUBHEAP  *next;       /* Next sub-heap */
    struct tagHEAP     *heap;       /* Main heap structure */
    DWORD               magic;      /* Magic number */
    WORD                selector;   /* Selector for HEAP_WINE_SEGPTR heaps */
} SUBHEAP;

#define SUBHEAP_MAGIC    ((DWORD)('S' | ('U'<<8) | ('B'<<16) | ('H'<<24)))

typedef struct tagHEAP
{
    SUBHEAP          subheap;       /* First sub-heap */
    struct tagHEAP  *next;          /* Next heap for this process */
    FREE_LIST_ENTRY  freeList[HEAP_NB_FREE_LISTS];  /* Free lists */
    CRITICAL_SECTION critSection;   /* Critical section for serialization */
    DWORD            flags;         /* Heap flags */
    DWORD            magic;         /* Magic number */
} HEAP;

#define HEAP_MAGIC       ((DWORD)('H' | ('E'<<8) | ('A'<<16) | ('P'<<24)))

#define HEAP_DEF_SIZE        0x110000   /* Default heap size = 1Mb + 64Kb */
#define HEAP_MIN_BLOCK_SIZE  (8+sizeof(ARENA_FREE))  /* Min. heap block size */

HANDLE32 SystemHeap = 0;
HANDLE32 SegptrHeap = 0;
CRITICAL_SECTION *HEAP_SystemLock = NULL;


/***********************************************************************
 *           HEAP_Dump
 */
void HEAP_Dump( HEAP *heap )
{
    int i;
    SUBHEAP *subheap;
    char *ptr;

    DUMP( "Heap: %08lx\n", (DWORD)heap );
    DUMP( "Next: %08lx  Sub-heaps: %08lx",
	  (DWORD)heap->next, (DWORD)&heap->subheap );
    subheap = &heap->subheap;
    while (subheap->next)
    {
        DUMP( " -> %08lx", (DWORD)subheap->next );
        subheap = subheap->next;
    }

    DUMP( "\nFree lists:\n Block   Stat   Size    Id\n" );
    for (i = 0; i < HEAP_NB_FREE_LISTS; i++)
        DUMP( "%08lx free %08lx %04x prev=%08lx next=%08lx\n",
	      (DWORD)&heap->freeList[i].arena, heap->freeList[i].arena.size,
	      heap->freeList[i].arena.threadId,
	      (DWORD)heap->freeList[i].arena.prev,
	      (DWORD)heap->freeList[i].arena.next );

    subheap = &heap->subheap;
    while (subheap)
    {
        DWORD freeSize = 0, usedSize = 0, arenaSize = subheap->headerSize;
        DUMP( "\n\nSub-heap %08lx: size=%08lx committed=%08lx\n",
	      (DWORD)subheap, subheap->size, subheap->commitSize );
	
        DUMP( "\n Block   Stat   Size    Id\n" );
        ptr = (char*)subheap + subheap->headerSize;
        while (ptr < (char *)subheap + subheap->size)
        {
            if (*(DWORD *)ptr & ARENA_FLAG_FREE)
            {
                ARENA_FREE *pArena = (ARENA_FREE *)ptr;
                DUMP( "%08lx free %08lx %04x prev=%08lx next=%08lx\n",
		      (DWORD)pArena, pArena->size & ARENA_SIZE_MASK,
		      pArena->threadId, (DWORD)pArena->prev,
		      (DWORD)pArena->next);
                ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
                arenaSize += sizeof(ARENA_FREE);
                freeSize += pArena->size & ARENA_SIZE_MASK;
            }
            else if (*(DWORD *)ptr & ARENA_FLAG_PREV_FREE)
            {
                ARENA_INUSE *pArena = (ARENA_INUSE *)ptr;
                DUMP( "%08lx Used %08lx %04x back=%08lx EIP=%08lx\n",
		      (DWORD)pArena, pArena->size & ARENA_SIZE_MASK,
		      pArena->threadId, *((DWORD *)pArena - 1),
		      pArena->callerEIP );
                ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
                arenaSize += sizeof(ARENA_INUSE);
                usedSize += pArena->size & ARENA_SIZE_MASK;
            }
            else
            {
                ARENA_INUSE *pArena = (ARENA_INUSE *)ptr;
                DUMP( "%08lx used %08lx %04x EIP=%08lx\n",
		      (DWORD)pArena, pArena->size & ARENA_SIZE_MASK,
		      pArena->threadId, pArena->callerEIP );
                ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
                arenaSize += sizeof(ARENA_INUSE);
                usedSize += pArena->size & ARENA_SIZE_MASK;
            }
        }
        DUMP( "\nTotal: Size=%08lx Committed=%08lx Free=%08lx Used=%08lx Arenas=%08lx (%ld%%)\n\n",
	      subheap->size, subheap->commitSize, freeSize, usedSize,
	      arenaSize, (arenaSize * 100) / subheap->size );
        subheap = subheap->next;
    }
}


/***********************************************************************
 *           HEAP_GetPtr
 * RETURNS
 *	Pointer to the heap
 *	NULL: Failure
 */
static HEAP *HEAP_GetPtr(
             HANDLE32 heap /* [in] Handle to the heap */
) {
    HEAP *heapPtr = (HEAP *)heap;
    if (!heapPtr || (heapPtr->magic != HEAP_MAGIC))
    {
        WARN(heap, "Invalid heap %08x!\n", heap );
        SetLastError( ERROR_INVALID_HANDLE );
        return NULL;
    }
    if (TRACE_ON(heap) && !HeapValidate( heap, 0, NULL ))
    {
        HEAP_Dump( heapPtr );
        assert( FALSE );
        SetLastError( ERROR_INVALID_HANDLE );
        return NULL;
    }
    return heapPtr;
}


/***********************************************************************
 *           HEAP_InsertFreeBlock
 *
 * Insert a free block into the free list.
 */
static void HEAP_InsertFreeBlock( HEAP *heap, ARENA_FREE *pArena )
{
    FREE_LIST_ENTRY *pEntry = heap->freeList;
    while (pEntry->size < pArena->size) pEntry++;
    pArena->size      |= ARENA_FLAG_FREE;
    pArena->next       = pEntry->arena.next;
    pArena->next->prev = pArena;
    pArena->prev       = &pEntry->arena;
    pEntry->arena.next = pArena;
}


/***********************************************************************
 *           HEAP_FindSubHeap
 * Find the sub-heap containing a given address.
 *
 * RETURNS
 *	Pointer: Success
 *	NULL: Failure
 */
static SUBHEAP *HEAP_FindSubHeap(
                HEAP *heap, /* [in] Heap pointer */
                LPCVOID ptr /* [in] Address */
) {
    SUBHEAP *sub = &heap->subheap;
    while (sub)
    {
        if (((char *)ptr >= (char *)sub) &&
            ((char *)ptr < (char *)sub + sub->size)) return sub;
        sub = sub->next;
    }
    return NULL;
}


/***********************************************************************
 *           HEAP_Commit
 *
 * Make sure the heap storage is committed up to (not including) ptr.
 */
static BOOL32 HEAP_Commit( SUBHEAP *subheap, void *ptr )
{
    DWORD size = (DWORD)((char *)ptr - (char *)subheap);
    size = (size + 0xfff) & 0xfffff000;  /* Align size on a page boundary */
    if (size > subheap->size) size = subheap->size;
    if (size <= subheap->commitSize) return TRUE;
    if (!VirtualAlloc( (char *)subheap + subheap->commitSize,
                       size - subheap->commitSize, MEM_COMMIT,
                       PAGE_EXECUTE_READWRITE))
    {
        WARN(heap, "Could not commit %08lx bytes at %08lx for heap %08lx\n",
                 size - subheap->commitSize,
                 (DWORD)((char *)subheap + subheap->commitSize),
                 (DWORD)subheap->heap );
        return FALSE;
    }
    subheap->commitSize = size;
    return TRUE;
}


/***********************************************************************
 *           HEAP_Decommit
 *
 * If possible, decommit the heap storage from (including) 'ptr'.
 */
static BOOL32 HEAP_Decommit( SUBHEAP *subheap, void *ptr )
{
    DWORD size = (DWORD)((char *)ptr - (char *)subheap);
    size = (size + 0xfff) & 0xfffff000;  /* Align size on a page boundary */
    if (size >= subheap->commitSize) return TRUE;
    if (!VirtualFree( (char *)subheap + size,
                      subheap->commitSize - size, MEM_DECOMMIT ))
    {
        WARN(heap, "Could not decommit %08lx bytes at %08lx for heap %08lx\n",
                 subheap->commitSize - size,
                 (DWORD)((char *)subheap + size),
                 (DWORD)subheap->heap );
        return FALSE;
    }
    subheap->commitSize = size;
    return TRUE;
}


/***********************************************************************
 *           HEAP_CreateFreeBlock
 *
 * Create a free block at a specified address. 'size' is the size of the
 * whole block, including the new arena.
 */
static void HEAP_CreateFreeBlock( SUBHEAP *subheap, void *ptr, DWORD size )
{
    ARENA_FREE *pFree;

    /* Create a free arena */

    pFree = (ARENA_FREE *)ptr;
    pFree->threadId = GetCurrentTask();
    pFree->magic = ARENA_FREE_MAGIC;

    /* If debugging, erase the freed block content */

    if (TRACE_ON(heap))
    {
        char *pEnd = (char *)ptr + size;
        if (pEnd > (char *)subheap + subheap->commitSize)
            pEnd = (char *)subheap + subheap->commitSize;
        if (pEnd > (char *)(pFree + 1))
            memset( pFree + 1, ARENA_FREE_FILLER, pEnd - (char *)(pFree + 1) );
    }

    /* Check if next block is free also */

    if (((char *)ptr + size < (char *)subheap + subheap->size) &&
        (*(DWORD *)((char *)ptr + size) & ARENA_FLAG_FREE))
    {
        /* Remove the next arena from the free list */
        ARENA_FREE *pNext = (ARENA_FREE *)((char *)ptr + size);
        pNext->next->prev = pNext->prev;
        pNext->prev->next = pNext->next;
        size += (pNext->size & ARENA_SIZE_MASK) + sizeof(*pNext);
        if (TRACE_ON(heap))
            memset( pNext, ARENA_FREE_FILLER, sizeof(ARENA_FREE) );
    }

    /* Set the next block PREV_FREE flag and pointer */

    if ((char *)ptr + size < (char *)subheap + subheap->size)
    {
        DWORD *pNext = (DWORD *)((char *)ptr + size);
        *pNext |= ARENA_FLAG_PREV_FREE;
        *(ARENA_FREE **)(pNext - 1) = pFree;
    }

    /* Last, insert the new block into the free list */

    pFree->size = size - sizeof(*pFree);
    HEAP_InsertFreeBlock( subheap->heap, pFree );
}


/***********************************************************************
 *           HEAP_MakeInUseBlockFree
 *
 * Turn an in-use block into a free block. Can also decommit the end of
 * the heap, and possibly even free the sub-heap altogether.
 */
static void HEAP_MakeInUseBlockFree( SUBHEAP *subheap, ARENA_INUSE *pArena )
{
    ARENA_FREE *pFree;
    DWORD size = (pArena->size & ARENA_SIZE_MASK) + sizeof(*pArena);

    /* Check if we can merge with previous block */

    if (pArena->size & ARENA_FLAG_PREV_FREE)
    {
        pFree = *((ARENA_FREE **)pArena - 1);
        size += (pFree->size & ARENA_SIZE_MASK) + sizeof(ARENA_FREE);
        /* Remove it from the free list */
        pFree->next->prev = pFree->prev;
        pFree->prev->next = pFree->next;
    }
    else pFree = (ARENA_FREE *)pArena;

    /* Create a free block */

    HEAP_CreateFreeBlock( subheap, pFree, size );
    size = (pFree->size & ARENA_SIZE_MASK) + sizeof(ARENA_FREE);
    if ((char *)pFree + size < (char *)subheap + subheap->size)
        return;  /* Not the last block, so nothing more to do */

    /* Free the whole sub-heap if it's empty and not the original one */

    if (((char *)pFree == (char *)subheap + subheap->headerSize) &&
        (subheap != &subheap->heap->subheap))
    {
        SUBHEAP *pPrev = &subheap->heap->subheap;
        /* Remove the free block from the list */
        pFree->next->prev = pFree->prev;
        pFree->prev->next = pFree->next;
        /* Remove the subheap from the list */
        while (pPrev && (pPrev->next != subheap)) pPrev = pPrev->next;
        if (pPrev) pPrev->next = subheap->next;
        /* Free the memory */
        subheap->magic = 0;
        if (subheap->selector) FreeSelector( subheap->selector );
        VirtualFree( subheap, 0, MEM_RELEASE );
        return;
    }
    
    /* Decommit the end of the heap */

    HEAP_Decommit( subheap, pFree + 1 );
}


/***********************************************************************
 *           HEAP_ShrinkBlock
 *
 * Shrink an in-use block.
 */
static void HEAP_ShrinkBlock(SUBHEAP *subheap, ARENA_INUSE *pArena, DWORD size)
{
    if ((pArena->size & ARENA_SIZE_MASK) >= size + HEAP_MIN_BLOCK_SIZE)
    {
        HEAP_CreateFreeBlock( subheap, (char *)(pArena + 1) + size,
                              (pArena->size & ARENA_SIZE_MASK) - size );
        pArena->size = (pArena->size & ~ARENA_SIZE_MASK) | size;
    }
    else
    {
        /* Turn off PREV_FREE flag in next block */
        char *pNext = (char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK);
        if (pNext < (char *)subheap + subheap->size)
            *(DWORD *)pNext &= ~ARENA_FLAG_PREV_FREE;
    }
}


/***********************************************************************
 *           HEAP_CreateSubHeap
 *
 * Create a sub-heap of the given size.
 */
static SUBHEAP *HEAP_CreateSubHeap( DWORD flags, DWORD commitSize,
                                    DWORD totalSize )
{
    SUBHEAP *subheap;
    WORD selector = 0;

    /* Round-up sizes on a 64K boundary */

    if (flags & HEAP_WINE_SEGPTR)
    {
        totalSize = commitSize = 0x10000;  /* Only 64K at a time for SEGPTRs */
    }
    else
    {
        totalSize  = (totalSize + 0xffff) & 0xffff0000;
        commitSize = (commitSize + 0xffff) & 0xffff0000;
        if (!commitSize) commitSize = 0x10000;
        if (totalSize < commitSize) totalSize = commitSize;
    }

    /* Allocate the memory block */

    if (!(subheap = VirtualAlloc( NULL, totalSize,
                                  MEM_RESERVE, PAGE_EXECUTE_READWRITE )))
    {
        WARN(heap, "Could not VirtualAlloc %08lx bytes\n",
                 totalSize );
        return NULL;
    }
    if (!VirtualAlloc(subheap, commitSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
    {
        WARN(heap, "Could not commit %08lx bytes for sub-heap %08lx\n",
                 commitSize, (DWORD)subheap );
        VirtualFree( subheap, 0, MEM_RELEASE );
        return NULL;
    }

    /* Allocate a selector if needed */

    if (flags & HEAP_WINE_SEGPTR)
    {
        selector = SELECTOR_AllocBlock( subheap, totalSize,
                     (flags & HEAP_WINE_CODESEG) ? SEGMENT_CODE : SEGMENT_DATA,
                     (flags & HEAP_WINE_CODESEG) != 0, FALSE );
        if (!selector)
        {
            WARN(heap, "Could not allocate selector\n" );
            VirtualFree( subheap, 0, MEM_RELEASE );
            return NULL;
        }
    }

    /* Fill the sub-heap structure */

    subheap->size       = totalSize;
    subheap->commitSize = commitSize;
    subheap->headerSize = sizeof(*subheap);
    subheap->next       = NULL;
    subheap->heap       = NULL;
    subheap->magic      = SUBHEAP_MAGIC;
    subheap->selector   = selector;
    return subheap;
}


/***********************************************************************
 *           HEAP_FindFreeBlock
 *
 * Find a free block at least as large as the requested size, and make sure
 * the requested size is committed.
 */
static ARENA_FREE *HEAP_FindFreeBlock( HEAP *heap, DWORD size,
                                       SUBHEAP **ppSubHeap )
{
    SUBHEAP *subheap;
    ARENA_FREE *pArena;
    FREE_LIST_ENTRY *pEntry = heap->freeList;

    /* Find a suitable free list, and in it find a block large enough */

    while (pEntry->size < size) pEntry++;
    pArena = pEntry->arena.next;
    while (pArena != &heap->freeList[0].arena)
    {
        if (pArena->size > size)
        {
            subheap = HEAP_FindSubHeap( heap, pArena );
            if (!HEAP_Commit( subheap, (char *)pArena + sizeof(ARENA_INUSE)
                                               + size + HEAP_MIN_BLOCK_SIZE))
                return NULL;
            *ppSubHeap = subheap;
            return pArena;
        }

        pArena = pArena->next;
    }

    /* If no block was found, attempt to grow the heap */

    if (!(heap->flags & HEAP_GROWABLE))
    {
        WARN(heap, "Not enough space in heap %08lx for %08lx bytes\n",
                 (DWORD)heap, size );
        return NULL;
    }
    size += sizeof(SUBHEAP) + sizeof(ARENA_FREE);
    if (!(subheap = HEAP_CreateSubHeap( heap->flags, size,
                                        MAX( HEAP_DEF_SIZE, size ) )))
        return NULL;

    /* Insert the new sub-heap in the list */

    subheap->heap = heap;
    subheap->next = heap->subheap.next;
    heap->subheap.next = subheap;
    size = subheap->size;
    TRACE(heap, "created new sub-heap %08lx of %08lx bytes for heap %08lx\n",
                  (DWORD)subheap, size, (DWORD)heap );

    HEAP_CreateFreeBlock( subheap, subheap + 1, size - sizeof(*subheap) );
    *ppSubHeap = subheap;
    return (ARENA_FREE *)(subheap + 1);
}


/***********************************************************************
 *           HEAP_IsValidArenaPtr
 *
 * Check that the pointer is inside the range possible for arenas.
 */
static BOOL32 HEAP_IsValidArenaPtr( HEAP *heap, void *ptr )
{
    int i;
    SUBHEAP *subheap = HEAP_FindSubHeap( heap, ptr );
    if (!subheap) return FALSE;
    if ((char *)ptr >= (char *)subheap + subheap->headerSize) return TRUE;
    if (subheap != &heap->subheap) return FALSE;
    for (i = 0; i < HEAP_NB_FREE_LISTS; i++)
        if (ptr == (void *)&heap->freeList[i].arena) return TRUE;
    return FALSE;
}


/***********************************************************************
 *           HEAP_ValidateFreeArena
 */
static BOOL32 HEAP_ValidateFreeArena( SUBHEAP *subheap, ARENA_FREE *pArena )
{
    char *heapEnd = (char *)subheap + subheap->size;

    /* Check magic number */
    if (pArena->magic != ARENA_FREE_MAGIC)
    {
        WARN(heap, "Heap %08lx: invalid free arena magic for %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena );
        return FALSE;
    }
    /* Check size flags */
    if (!(pArena->size & ARENA_FLAG_FREE) ||
        (pArena->size & ARENA_FLAG_PREV_FREE))
    {
        WARN(heap, "Heap %08lx: bad flags %lx for free arena %08lx\n",
                 (DWORD)subheap->heap, pArena->size & ~ARENA_SIZE_MASK, (DWORD)pArena );
    }
    /* Check arena size */
    if ((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK) > heapEnd)
    {
        WARN(heap, "Heap %08lx: bad size %08lx for free arena %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena->size & ARENA_SIZE_MASK, (DWORD)pArena );
        return FALSE;
    }
    /* Check that next pointer is valid */
    if (!HEAP_IsValidArenaPtr( subheap->heap, pArena->next ))
    {
        WARN(heap, "Heap %08lx: bad next ptr %08lx for arena %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena->next, (DWORD)pArena );
        return FALSE;
    }
    /* Check that next arena is free */
    if (!(pArena->next->size & ARENA_FLAG_FREE) ||
        (pArena->next->magic != ARENA_FREE_MAGIC))
    { 
        WARN(heap, "Heap %08lx: next arena %08lx invalid for %08lx\n", 
                 (DWORD)subheap->heap, (DWORD)pArena->next, (DWORD)pArena );
        return FALSE;
    }
    /* Check that prev pointer is valid */
    if (!HEAP_IsValidArenaPtr( subheap->heap, pArena->prev ))
    {
        WARN(heap, "Heap %08lx: bad prev ptr %08lx for arena %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena->prev, (DWORD)pArena );
        return FALSE;
    }
    /* Check that prev arena is free */
    if (!(pArena->prev->size & ARENA_FLAG_FREE) ||
        (pArena->prev->magic != ARENA_FREE_MAGIC))
    { 
        WARN(heap, "Heap %08lx: prev arena %08lx invalid for %08lx\n", 
                 (DWORD)subheap->heap, (DWORD)pArena->prev, (DWORD)pArena );
        return FALSE;
    }
    /* Check that next block has PREV_FREE flag */
    if ((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK) < heapEnd)
    {
        if (!(*(DWORD *)((char *)(pArena + 1) +
            (pArena->size & ARENA_SIZE_MASK)) & ARENA_FLAG_PREV_FREE))
        {
            WARN(heap, "Heap %08lx: free arena %08lx next block has no PREV_FREE flag\n",
                     (DWORD)subheap->heap, (DWORD)pArena );
            return FALSE;
        }
        /* Check next block back pointer */
        if (*((ARENA_FREE **)((char *)(pArena + 1) +
            (pArena->size & ARENA_SIZE_MASK)) - 1) != pArena)
        {
            WARN(heap, "Heap %08lx: arena %08lx has wrong back ptr %08lx\n",
                     (DWORD)subheap->heap, (DWORD)pArena,
                     *((DWORD *)((char *)(pArena+1)+ (pArena->size & ARENA_SIZE_MASK)) - 1));
            return FALSE;
        }
    }
    return TRUE;
}


/***********************************************************************
 *           HEAP_ValidateInUseArena
 */
static BOOL32 HEAP_ValidateInUseArena( SUBHEAP *subheap, ARENA_INUSE *pArena )
{
    char *heapEnd = (char *)subheap + subheap->size;

    /* Check magic number */
    if (pArena->magic != ARENA_INUSE_MAGIC)
    {
        WARN(heap, "Heap %08lx: invalid in-use arena magic for %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena );
        return FALSE;
    }
    /* Check size flags */
    if (pArena->size & ARENA_FLAG_FREE) 
    {
        WARN(heap, "Heap %08lx: bad flags %lx for in-use arena %08lx\n",
                 (DWORD)subheap->heap, pArena->size & ~ARENA_SIZE_MASK, (DWORD)pArena );
    }
    /* Check arena size */
    if ((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK) > heapEnd)
    {
        WARN(heap, "Heap %08lx: bad size %08lx for in-use arena %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena->size & ARENA_SIZE_MASK, (DWORD)pArena );
        return FALSE;
    }
    /* Check next arena PREV_FREE flag */
    if (((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK) < heapEnd) &&
        (*(DWORD *)((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK)) & ARENA_FLAG_PREV_FREE))
    {
        WARN(heap, "Heap %08lx: in-use arena %08lx next block has PREV_FREE flag\n",
                 (DWORD)subheap->heap, (DWORD)pArena );
        return FALSE;
    }
    /* Check prev free arena */
    if (pArena->size & ARENA_FLAG_PREV_FREE)
    {
        ARENA_FREE *pPrev = *((ARENA_FREE **)pArena - 1);
        /* Check prev pointer */
        if (!HEAP_IsValidArenaPtr( subheap->heap, pPrev ))
        {
            WARN(heap, "Heap %08lx: bad back ptr %08lx for arena %08lx\n",
                    (DWORD)subheap->heap, (DWORD)pPrev, (DWORD)pArena );
            return FALSE;
        }
        /* Check that prev arena is free */
        if (!(pPrev->size & ARENA_FLAG_FREE) ||
            (pPrev->magic != ARENA_FREE_MAGIC))
        { 
            WARN(heap, "Heap %08lx: prev arena %08lx invalid for in-use %08lx\n", 
                     (DWORD)subheap->heap, (DWORD)pPrev, (DWORD)pArena );
            return FALSE;
        }
        /* Check that prev arena is really the previous block */
        if ((char *)(pPrev + 1) + (pPrev->size & ARENA_SIZE_MASK) != (char *)pArena)
        {
            WARN(heap, "Heap %08lx: prev arena %08lx is not prev for in-use %08lx\n",
                     (DWORD)subheap->heap, (DWORD)pPrev, (DWORD)pArena );
            return FALSE;
        }
    }
    return TRUE;
}


/***********************************************************************
 *           HEAP_IsInsideHeap
 * Checks whether the pointer points to a block inside a given heap.
 *
 * NOTES
 *	Should this return BOOL32?
 *
 * RETURNS
 *	!0: Success
 *	0: Failure
 */
int HEAP_IsInsideHeap(
    HANDLE32 heap, /* [in] Heap */
    DWORD flags,   /* [in] Flags */
    LPCVOID ptr    /* [in] Pointer */
) {
    HEAP *heapPtr = HEAP_GetPtr( heap );
    SUBHEAP *subheap;
    int ret;

    /* Validate the parameters */

    if (!heapPtr) return 0;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) HeapLock( heap );
    ret = (((subheap = HEAP_FindSubHeap( heapPtr, ptr )) != NULL) &&
           (((char *)ptr >= (char *)subheap + subheap->headerSize
                              + sizeof(ARENA_INUSE))));
    if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );
    return ret;
}


/***********************************************************************
 *           HEAP_GetSegptr
 *
 * Transform a linear pointer into a SEGPTR. The pointer must have been
 * allocated from a HEAP_WINE_SEGPTR heap.
 */
SEGPTR HEAP_GetSegptr( HANDLE32 heap, DWORD flags, LPCVOID ptr )
{
    HEAP *heapPtr = HEAP_GetPtr( heap );
    SUBHEAP *subheap;
    SEGPTR ret;

    /* Validate the parameters */

    if (!heapPtr) return 0;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_WINE_SEGPTR))
    {
        WARN(heap, "Heap %08x is not a SEGPTR heap\n",
                 heap );
        return 0;
    }
    if (!(flags & HEAP_NO_SERIALIZE)) HeapLock( heap );

    /* Get the subheap */

    if (!(subheap = HEAP_FindSubHeap( heapPtr, ptr )))
    {
        WARN(heap, "%p is not inside heap %08x\n",
                 ptr, heap );
        if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );
        return 0;
    }

    /* Build the SEGPTR */

    ret = PTR_SEG_OFF_TO_SEGPTR(subheap->selector, (DWORD)ptr-(DWORD)subheap);
    if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );
    return ret;
}


/***********************************************************************
 *           HeapCreate   (KERNEL32.336)
 * RETURNS
 *	Handle of heap: Success
 *	NULL: Failure
 */
HANDLE32 WINAPI HeapCreate(
                DWORD flags,       /* [in] Heap allocation flag */
                DWORD initialSize, /* [in] Initial heap size */
                DWORD maxSize      /* [in] Maximum heap size */
) {
    int i;
    HEAP *heap;
    SUBHEAP *subheap;
    FREE_LIST_ENTRY *pEntry;

    /* Allocate the heap block */

    if (!maxSize)
    {
        maxSize = HEAP_DEF_SIZE;
        flags |= HEAP_GROWABLE;
    }
    if (!(subheap = HEAP_CreateSubHeap( flags, initialSize, maxSize )))
    {
        SetLastError( ERROR_OUTOFMEMORY );
        return 0;
    }

    /* Fill the heap structure */

    heap = (HEAP *)subheap;
    subheap->heap       = heap;
    subheap->headerSize = sizeof(HEAP);
    heap->next          = NULL;
    heap->flags         = flags;
    heap->magic         = HEAP_MAGIC;

    /* Build the free lists */

    for (i = 0, pEntry = heap->freeList; i < HEAP_NB_FREE_LISTS; i++, pEntry++)
    {
        pEntry->size           = HEAP_freeListSizes[i];
        pEntry->arena.size     = 0 | ARENA_FLAG_FREE;
        pEntry->arena.next     = i < HEAP_NB_FREE_LISTS-1 ?
                         &heap->freeList[i+1].arena : &heap->freeList[0].arena;
        pEntry->arena.prev     = i ? &heap->freeList[i-1].arena : 
                                   &heap->freeList[HEAP_NB_FREE_LISTS-1].arena;
        pEntry->arena.threadId = 0;
        pEntry->arena.magic    = ARENA_FREE_MAGIC;
    }

    /* Create the first free block */

    HEAP_CreateFreeBlock( subheap, heap + 1, subheap->size - sizeof(*heap) );

    /* Initialize critical section */

    InitializeCriticalSection( &heap->critSection );
    if (!SystemHeap) HEAP_SystemLock = &heap->critSection;

    /* We are done */

    return (HANDLE32)heap;
}


/***********************************************************************
 *           HeapDestroy   (KERNEL32.337)
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL32 WINAPI HeapDestroy(
              HANDLE32 heap /* [in] Handle of heap */
) {
    HEAP *heapPtr = HEAP_GetPtr( heap );
    SUBHEAP *subheap;

    TRACE(heap, "%08x\n", heap );
    if (!heapPtr) return FALSE;

    DeleteCriticalSection( &heapPtr->critSection );
    subheap = &heapPtr->subheap;
    while (subheap)
    {
        SUBHEAP *next = subheap->next;
        if (subheap->selector) FreeSelector( subheap->selector );
        VirtualFree( subheap, 0, MEM_RELEASE );
        subheap = next;
    }
    return TRUE;
}


/***********************************************************************
 *           HeapAlloc   (KERNEL32.334)
 * RETURNS
 *	Pointer to allocated memory block
 *	NULL: Failure
 */
LPVOID WINAPI HeapAlloc(
              HANDLE32 heap, /* [in] Handle of private heap block */
              DWORD flags,   /* [in] Heap allocation control flags */
              DWORD size     /* [in] Number of bytes to allocate */
) {
    ARENA_FREE *pArena;
    ARENA_INUSE *pInUse;
    SUBHEAP *subheap;
    HEAP *heapPtr = HEAP_GetPtr( heap );

    /* Validate the parameters */

    if (!heapPtr) return NULL;
    flags &= HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) HeapLock( heap );
    size = (size + 3) & ~3;
    if (size < HEAP_MIN_BLOCK_SIZE) size = HEAP_MIN_BLOCK_SIZE;

    /* Locate a suitable free block */

    if (!(pArena = HEAP_FindFreeBlock( heapPtr, size, &subheap )))
    {
        TRACE(heap, "(%08x,%08lx,%08lx): returning NULL\n",
                  heap, flags, size  );
        if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );
        SetLastError( ERROR_COMMITMENT_LIMIT );
        return NULL;
    }

    /* Remove the arena from the free list */

    pArena->next->prev = pArena->prev;
    pArena->prev->next = pArena->next;

    /* Build the in-use arena */

    pInUse = (ARENA_INUSE *)pArena;
    pInUse->size      = (pInUse->size & ~ARENA_FLAG_FREE)
                        + sizeof(ARENA_FREE) - sizeof(ARENA_INUSE);
    pInUse->callerEIP = *((DWORD *)&heap - 1);  /* hack hack */
    pInUse->threadId  = GetCurrentTask();
    pInUse->magic     = ARENA_INUSE_MAGIC;

    /* Shrink the block */

    HEAP_ShrinkBlock( subheap, pInUse, size );

    if (flags & HEAP_ZERO_MEMORY) memset( pInUse + 1, 0, size );
    else if (TRACE_ON(heap)) memset( pInUse + 1, ARENA_INUSE_FILLER, size );

    if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );

    TRACE(heap, "(%08x,%08lx,%08lx): returning %08lx\n",
                  heap, flags, size, (DWORD)(pInUse + 1) );
    return (LPVOID)(pInUse + 1);
}


/***********************************************************************
 *           HeapFree   (KERNEL32.338)
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL32 WINAPI HeapFree(
              HANDLE32 heap, /* [in] Handle of heap */
              DWORD flags,   /* [in] Heap freeing flags */
              LPVOID ptr     /* [in] Address of memory to free */
) {
    ARENA_INUSE *pInUse;
    SUBHEAP *subheap;
    HEAP *heapPtr = HEAP_GetPtr( heap );

    /* Validate the parameters */

    if (!heapPtr) return FALSE;
    flags &= HEAP_NO_SERIALIZE;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) HeapLock( heap );
    if (!ptr || !HeapValidate( heap, HEAP_NO_SERIALIZE, ptr ))
    {
        if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );
        SetLastError( ERROR_INVALID_PARAMETER );
        TRACE(heap, "(%08x,%08lx,%08lx): returning FALSE\n",
                      heap, flags, (DWORD)ptr );
        return FALSE;
    }

    /* Turn the block into a free block */

    pInUse  = (ARENA_INUSE *)ptr - 1;
    subheap = HEAP_FindSubHeap( heapPtr, pInUse );
    HEAP_MakeInUseBlockFree( subheap, pInUse );

    if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );
/*    SetLastError( 0 ); */

    TRACE(heap, "(%08x,%08lx,%08lx): returning TRUE\n",
                  heap, flags, (DWORD)ptr );
    return TRUE;
}


/***********************************************************************
 *           HeapReAlloc   (KERNEL32.340)
 * RETURNS
 *	Pointer to reallocated memory block
 *	NULL: Failure
 */
LPVOID WINAPI HeapReAlloc(
              HANDLE32 heap, /* [in] Handle of heap block */
              DWORD flags,   /* [in] Heap reallocation flags */
              LPVOID ptr,    /* [in] Address of memory to reallocate */
              DWORD size     /* [in] Number of bytes to reallocate */
) {
    ARENA_INUSE *pArena;
    DWORD oldSize;
    HEAP *heapPtr;
    SUBHEAP *subheap;

    if (!ptr) return HeapAlloc( heap, flags, size );  /* FIXME: correct? */
    if (!(heapPtr = HEAP_GetPtr( heap ))) return FALSE;

    /* Validate the parameters */

    flags &= HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY |
             HEAP_REALLOC_IN_PLACE_ONLY;
    flags |= heapPtr->flags;
    size = (size + 3) & ~3;
    if (size < HEAP_MIN_BLOCK_SIZE) size = HEAP_MIN_BLOCK_SIZE;

    if (!(flags & HEAP_NO_SERIALIZE)) HeapLock( heap );
    if (!HeapValidate( heap, HEAP_NO_SERIALIZE, ptr ))
    {
        if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );
        SetLastError( ERROR_INVALID_PARAMETER );
        TRACE(heap, "(%08x,%08lx,%08lx,%08lx): returning NULL\n",
                      heap, flags, (DWORD)ptr, size );
        return NULL;
    }

    /* Check if we need to grow the block */

    pArena = (ARENA_INUSE *)ptr - 1;
    pArena->threadId = GetCurrentTask();
    subheap = HEAP_FindSubHeap( heapPtr, pArena );
    oldSize = (pArena->size & ARENA_SIZE_MASK);
    if (size > oldSize)
    {
        char *pNext = (char *)(pArena + 1) + oldSize;
        if ((pNext < (char *)subheap + subheap->size) &&
            (*(DWORD *)pNext & ARENA_FLAG_FREE) &&
            (oldSize + (*(DWORD *)pNext & ARENA_SIZE_MASK) + sizeof(ARENA_FREE) >= size))
        {
            /* The next block is free and large enough */
            ARENA_FREE *pFree = (ARENA_FREE *)pNext;
            pFree->next->prev = pFree->prev;
            pFree->prev->next = pFree->next;
            pArena->size += (pFree->size & ARENA_SIZE_MASK) + sizeof(*pFree);
            if (!HEAP_Commit( subheap, (char *)pArena + sizeof(ARENA_INUSE)
                                               + size + HEAP_MIN_BLOCK_SIZE))
            {
                if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );
                SetLastError( ERROR_OUTOFMEMORY );
                return NULL;
            }
            HEAP_ShrinkBlock( subheap, pArena, size );
        }
        else  /* Do it the hard way */
        {
            ARENA_FREE *pNew;
            ARENA_INUSE *pInUse;
            SUBHEAP *newsubheap;

            if ((flags & HEAP_REALLOC_IN_PLACE_ONLY) ||
                !(pNew = HEAP_FindFreeBlock( heapPtr, size, &newsubheap )))
            {
                if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );
                SetLastError( ERROR_OUTOFMEMORY );
                return NULL;
            }

            /* Build the in-use arena */

            pNew->next->prev = pNew->prev;
            pNew->prev->next = pNew->next;
            pInUse = (ARENA_INUSE *)pNew;
            pInUse->size     = (pInUse->size & ~ARENA_FLAG_FREE)
                               + sizeof(ARENA_FREE) - sizeof(ARENA_INUSE);
            pInUse->threadId = GetCurrentTask();
            pInUse->magic    = ARENA_INUSE_MAGIC;
            HEAP_ShrinkBlock( newsubheap, pInUse, size );
            memcpy( pInUse + 1, pArena + 1, oldSize );

            /* Free the previous block */

            HEAP_MakeInUseBlockFree( subheap, pArena );
            subheap = newsubheap;
            pArena  = pInUse;
        }
    }
    else HEAP_ShrinkBlock( subheap, pArena, size );  /* Shrink the block */

    /* Clear the extra bytes if needed */

    if (size > oldSize)
    {
        if (flags & HEAP_ZERO_MEMORY)
            memset( (char *)(pArena + 1) + oldSize, 0,
                    (pArena->size & ARENA_SIZE_MASK) - oldSize );
        else if (TRACE_ON(heap))
            memset( (char *)(pArena + 1) + oldSize, ARENA_INUSE_FILLER,
                    (pArena->size & ARENA_SIZE_MASK) - oldSize );
    }

    /* Return the new arena */

    pArena->callerEIP = *((DWORD *)&heap - 1);  /* hack hack */
    if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );

    TRACE(heap, "(%08x,%08lx,%08lx,%08lx): returning %08lx\n",
                  heap, flags, (DWORD)ptr, size, (DWORD)(pArena + 1) );
    return (LPVOID)(pArena + 1);
}


/***********************************************************************
 *           HeapCompact   (KERNEL32.335)
 */
DWORD WINAPI HeapCompact( HANDLE32 heap, DWORD flags )
{
    return 0;
}


/***********************************************************************
 *           HeapLock   (KERNEL32.339)
 * Attempts to acquire the critical section object for a specified heap.
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL32 WINAPI HeapLock(
              HANDLE32 heap /* [in] Handle of heap to lock for exclusive access */
) {
    HEAP *heapPtr = HEAP_GetPtr( heap );
    if (!heapPtr) return FALSE;
    EnterCriticalSection( &heapPtr->critSection );
    return TRUE;
}


/***********************************************************************
 *           HeapUnlock   (KERNEL32.342)
 * Releases ownership of the critical section object.
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL32 WINAPI HeapUnlock(
              HANDLE32 heap /* [in] Handle to the heap to unlock */
) {
    HEAP *heapPtr = HEAP_GetPtr( heap );
    if (!heapPtr) return FALSE;
    LeaveCriticalSection( &heapPtr->critSection );
    return TRUE;
}


/***********************************************************************
 *           HeapSize   (KERNEL32.341)
 * RETURNS
 *	Size in bytes of allocated memory
 *	0: Failure
 */
DWORD WINAPI HeapSize(
             HANDLE32 heap, /* [in] Handle of heap */
             DWORD flags,   /* [in] Heap size control flags */
             LPVOID ptr     /* [in] Address of memory to return size for */
) {
    DWORD ret;
    HEAP *heapPtr = HEAP_GetPtr( heap );

    if (!heapPtr) return FALSE;
    flags &= HEAP_NO_SERIALIZE;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) HeapLock( heap );
    if (!HeapValidate( heap, HEAP_NO_SERIALIZE, ptr ))
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        ret = 0xffffffff;
    }
    else
    {
        ARENA_INUSE *pArena = (ARENA_INUSE *)ptr - 1;
        ret = pArena->size & ARENA_SIZE_MASK;
    }
    if (!(flags & HEAP_NO_SERIALIZE)) HeapUnlock( heap );

    TRACE(heap, "(%08x,%08lx,%08lx): returning %08lx\n",
                  heap, flags, (DWORD)ptr, ret );
    return ret;
}


/***********************************************************************
 *           HeapValidate   (KERNEL32.343)
 * Validates a specified heap.
 *
 * NOTES
 *	Flags is ignored.
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL32 WINAPI HeapValidate(
              HANDLE32 heap, /* [in] Handle to the heap */
              DWORD flags,   /* [in] Bit flags that control access during operation */
              LPCVOID block  /* [in] Optional pointer to memory block to validate */
) {
    SUBHEAP *subheap;
    HEAP *heapPtr = (HEAP *)(heap);

    if (!heapPtr || (heapPtr->magic != HEAP_MAGIC))
    {
        WARN(heap, "Invalid heap %08x!\n", heap );
        return FALSE;
    }

    if (block)
    {
        /* Only check this single memory block */
        if (!(subheap = HEAP_FindSubHeap( heapPtr, block )) ||
            ((char *)block < (char *)subheap + subheap->headerSize
                              + sizeof(ARENA_INUSE)))
        {
            WARN(heap, "Heap %08lx: block %08lx is not inside heap\n",
                     (DWORD)heap, (DWORD)block );
            return FALSE;
        }
        return HEAP_ValidateInUseArena( subheap, (ARENA_INUSE *)block - 1 );
    }

    subheap = &heapPtr->subheap;
    while (subheap)
    {
        char *ptr = (char *)subheap + subheap->headerSize;
        while (ptr < (char *)subheap + subheap->size)
        {
            if (*(DWORD *)ptr & ARENA_FLAG_FREE)
            {
                if (!HEAP_ValidateFreeArena( subheap, (ARENA_FREE *)ptr ))
                    return FALSE;
                ptr += sizeof(ARENA_FREE) + (*(DWORD *)ptr & ARENA_SIZE_MASK);
            }
            else
            {
                if (!HEAP_ValidateInUseArena( subheap, (ARENA_INUSE *)ptr ))
                    return FALSE;
                ptr += sizeof(ARENA_INUSE) + (*(DWORD *)ptr & ARENA_SIZE_MASK);
            }
        }
        subheap = subheap->next;
    }
    return TRUE;
}


/***********************************************************************
 *           HeapWalk   (KERNEL32.344)
 * Enumerates the memory blocks in a specified heap.
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL32 WINAPI HeapWalk(
              HANDLE32 heap,               /* [in]  Handle to heap to enumerate */
              LPPROCESS_HEAP_ENTRY *entry  /* [out] Pointer to structure of enumeration info */
) {
    FIXME(heap, "(%08x): stub.\n", heap );
    return FALSE;
}


/***********************************************************************
 *           HEAP_xalloc
 *
 * Same as HeapAlloc(), but die on failure.
 */
LPVOID HEAP_xalloc( HANDLE32 heap, DWORD flags, DWORD size )
{
    LPVOID p = HeapAlloc( heap, flags, size );
    if (!p)
    {
        MSG("Virtual memory exhausted.\n" );
        exit(1);
    }
    return p;
}


/***********************************************************************
 *           HEAP_strdupA
 */
LPSTR HEAP_strdupA( HANDLE32 heap, DWORD flags, LPCSTR str )
{
    LPSTR p = HEAP_xalloc( heap, flags, lstrlen32A(str) + 1 );
    lstrcpy32A( p, str );
    return p;
}


/***********************************************************************
 *           HEAP_strdupW
 */
LPWSTR HEAP_strdupW( HANDLE32 heap, DWORD flags, LPCWSTR str )
{
    INT32 len = lstrlen32W(str) + 1;
    LPWSTR p = HEAP_xalloc( heap, flags, len * sizeof(WCHAR) );
    lstrcpy32W( p, str );
    return p;
}


/***********************************************************************
 *           HEAP_strdupAtoW
 */
LPWSTR HEAP_strdupAtoW( HANDLE32 heap, DWORD flags, LPCSTR str )
{
    LPWSTR ret;

    if (!str) return NULL;
    ret = HEAP_xalloc( heap, flags, (lstrlen32A(str)+1) * sizeof(WCHAR) );
    lstrcpyAtoW( ret, str );
    return ret;
}


/***********************************************************************
 *           HEAP_strdupWtoA
 */
LPSTR HEAP_strdupWtoA( HANDLE32 heap, DWORD flags, LPCWSTR str )
{
    LPSTR ret;

    if (!str) return NULL;
    ret = HEAP_xalloc( heap, flags, lstrlen32W(str) + 1 );
    lstrcpyWtoA( ret, str );
    return ret;
}
