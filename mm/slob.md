/*
 * Linux 2.6.20 — mm/slob.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - Detailed background and overview
 *  - K&R allocator model
 *  - Allocation/free flow
 *  - Small vs large allocation paths
 *  - kmalloc/kmem_cache layering
 *  - Fragmentation + coalescing
 *
 * Source: mm/slob.c
 */


/***************************************************************
 * 0. BIG PICTURE (VERY IMPORTANT)
 ***************************************************************/

/*
SLOB = SIMPLE LIST OF BLOCKS allocator

Layering:

    kmalloc / kmem_cache
            ↓
         SLOB
            ↓
     __get_free_pages (buddy)

SLOB is the simplest kernel allocator used for:
    - embedded systems
    - low memory footprint
    - simplicity over performance
*/


/***************************************************************
 * 1. WHY SLOB EXISTS
 ***************************************************************/

/*
Compared to SLAB:

SLAB:
    + fast
    + per-CPU caching
    - complex

SLOB:
    + simple
    + minimal overhead
    - slow
    - global lock contention
*/


/***************************************************************
 * 2. CORE DESIGN (K&R ALLOCATOR)
 ***************************************************************/

/*
SLOB is a classic K&R heap allocator:

    - singly linked free list
    - first-fit allocation
    - split on alloc
    - merge on free
*/


/***************************************************************
 * 3. CORE STRUCTURE
 ***************************************************************/

struct slob_block {
    int units;
    struct slob_block *next;
};

#define SLOB_UNIT sizeof(struct slob_block)

/*
units:
    size of block in units

next:
    next free block
*/


/***************************************************************
 * 4. GLOBAL HEAP
 ***************************************************************/

static struct slob_block arena;
static struct slob_block *slobfree;

/*
Heap = circular linked list of free blocks
*/


/***************************************************************
 * 5. SMALL ALLOCATION FLOW
 ***************************************************************/

/*
Function: slob_alloc()

Algorithm: FIRST-FIT
*/


/*
Flow:

for each free block:
    if big enough:
        if exact fit:
            remove block
        else:
            split block
        return pointer
*/


/***************************************************************
 * 6. ALIGNMENT HANDLING
 ***************************************************************/

/*
If alignment required:

    aligned = ALIGN(cur, align)
    delta = aligned - cur

May split block head
*/


/***************************************************************
 * 7. HEAP GROWTH
 ***************************************************************/

/*
If no block found:

    cur = __get_free_page()
    slob_free(cur)

So heap grows from buddy allocator
*/


/***************************************************************
 * 8. FREE PATH (COALESCING)
 ***************************************************************/

/*
Function: slob_free()

Steps:

1. find insertion point
2. merge with next if adjacent
3. merge with prev if adjacent
*/


/***************************************************************
 * 9. LARGE ALLOCATIONS
 ***************************************************************/

/*
In __kmalloc():

if size >= PAGE_SIZE:
    use __get_free_pages()
*/


struct bigblock {
    int order;
    void *pages;
    struct bigblock *next;
};

/*
Large allocations tracked separately
*/


/***************************************************************
 * 10. kfree() FLOW
 ***************************************************************/

/*
if page aligned:
    free_pages()
else:
    slob_free()
*/


/***************************************************************
 * 11. kmalloc LAYER
 ***************************************************************/

/*
__kmalloc():

small:
    slob_alloc(size + header)

large:
    __get_free_pages()
*/


/***************************************************************
 * 12. kmem_cache (FAKE SLAB)
 ***************************************************************/

struct kmem_cache {
    size;
    align;
    ctor;
    dtor;
};

/*
SLOB emulates slab:
    - alloc memory
    - call ctor/dtor each time
*/


/***************************************************************
 * 13. kmem_cache_alloc()
 ***************************************************************/

/*
if small:
    slob_alloc()
else:
    alloc_pages()

then call ctor
*/


/***************************************************************
 * 14. kmem_cache_free()
 ***************************************************************/

/*
call dtor
free memory
*/


/***************************************************************
 * 15. LOCKING
 ***************************************************************/

/*
slob_lock   → protects free list
block_lock  → protects bigblocks

Global locks → poor scalability
*/


/***************************************************************
 * 16. FULL FLOW
 ***************************************************************/

/*
kmalloc()
    ↓
__kmalloc()
    ↓
small → slob_alloc()
        ↓
        search free list
        ↓
        split block

large → __get_free_pages()

kfree()
    ↓
if large → free_pages()
else → slob_free()
        ↓
        insert + merge
*/


/***************************************************************
 * 17. SLOB vs SLAB
 ***************************************************************/

/*
SLOB:
    simple, low memory, slow

SLAB:
    fast, complex, per-CPU
*/


/***************************************************************
 * 18. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/slob.c implements a simple K&R-style heap allocator using a
first-fit free list with splitting and coalescing, and falls back
to page allocation for large objects.
*/


/***************************************************************
 * END
 ***************************************************************/

