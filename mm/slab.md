/*
 * Linux 2.6.20 — mm/slab.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - Detailed background and overview
 *  - Why the slab allocator exists
 *  - Core data structures and memory layout
 *  - Allocation/free flow
 *  - Per-CPU caches, per-node lists, NUMA behavior
 *  - Bootstrapping, debug features, and mental model
 *
 * Source: Linux 2.6.20 mm/slab.c
 */


/***************************************************************
 * 0. BIG PICTURE (VERY IMPORTANT)
 ***************************************************************/

/*
This file implements the classic Linux SLAB allocator.

It sits BETWEEN:

    callers wanting kernel objects
and
    the page allocator which hands out raw pages

So the layering is:

    kmalloc / kmem_cache_alloc
            ↓
        SLAB allocator
            ↓
        alloc_pages / buddy allocator


The page allocator works in UNITS OF PAGES.
The slab allocator works in UNITS OF OBJECTS.

Examples of slab-allocated objects:
    - struct inode
    - struct dentry
    - struct vm_area_struct
    - kmalloc-64 / kmalloc-128 / kmalloc-256 objects
*/


/*
Core idea:

    Don't keep allocating raw pages for every tiny object.

Instead:
    - carve pages into many objects of the same size/type
    - cache initialized objects
    - reuse them quickly
    - keep hot objects CPU-local when possible

That is why slab exists.
*/


/***************************************************************
 * 1. WHY THE KERNEL NEEDS A SLAB ALLOCATOR
 ***************************************************************/

/*
If the kernel only had alloc_pages():

    - small allocations would waste memory
    - initialization cost would be repeated too often
    - fragmentation would get worse
    - object locality would be poor
    - synchronization overhead would be high


The slab allocator solves these problems by:

1. Object caching
   Each object type can have its own cache.

2. Reuse
   Freed objects can be reused without rebuilding everything from scratch.

3. Constructors/destructors
   New slabs can initialize objects once when created.

4. CPU locality
   Per-CPU object arrays reduce lock traffic and improve cache warmth.

5. Fragmentation reduction
   Pages are divided into equal-size objects for each cache.
*/


/***************************************************************
 * 2. KEY MENTAL MODEL
 ***************************************************************/

/*
A SLAB CACHE is like a warehouse for one object type.

A SLAB is one batch of storage inside that warehouse.

An OBJECT is one item from that slab.

So:

    kmem_cache
        contains many slabs
            each slab contains many objects
*/


/*
ASCII view:

    kmem_cache("inode_cache")
        |
        +--> slab A  [obj obj obj obj obj]
        +--> slab B  [obj obj obj obj obj]
        +--> slab C  [obj obj obj obj obj]


And each slab is in one of three states:

    full     -> no free objects
    partial  -> some free objects   (best place to allocate from)
    free     -> all objects free
*/


/***************************************************************
 * 3. MAIN DESIGN GOALS CALLED OUT BY THE FILE
 ***************************************************************/

/*
The header comments of slab.c already tell the story well.

Major design goals:

1. Organize memory in caches, one per object type.
2. Use slabs as small contiguous memory regions.
3. Reuse initialized objects.
4. Keep slabs split into full / partial / free groups.
5. Use per-CPU head arrays for fast allocation/free.
6. Support DMA / HIGHMEM / normal memory distinctions.
7. Support SMP and NUMA efficiently.
8. Provide debug options like poisoning and redzones.
*/


/***************************************************************
 * 4. VERY IMPORTANT STRUCTURES
 ***************************************************************/

/*
The most important structures in this file are:

    struct kmem_cache
    struct slab
    struct array_cache
    struct kmem_list3
*/


/***************************************************************
 * 5. struct slab
 ***************************************************************/

/*
A slab is one chunk of memory holding multiple objects.

struct slab {
    struct list_head list;
    unsigned long colouroff;
    void *s_mem;
    unsigned int inuse;
    kmem_bufctl_t free;
    unsigned short nodeid;
};

Meaning of important fields:

list:
    links slab into full/partial/free list

colouroff:
    cache coloring offset (reduce cache index conflicts)

s_mem:
    pointer to first object storage in slab

inuse:
    how many objects currently allocated

free:
    freelist head (index-based bufctl chain)

nodeid:
    NUMA node owning this slab
*/


/***************************************************************
 * 6. struct kmem_cache
 ***************************************************************/

/*
This represents one cache of objects.

It is the main control structure for a slab cache.

Important members:

array[NR_CPUS]:
    per-CPU fast path caches

batchcount / limit / shared:
    tuning knobs for per-CPU caches

buffer_size:
    size of each object in slab (may include debug overhead)

nodelists[MAX_NUMNODES]:
    per-node slab state

num:
    number of objects per slab

gfporder:
    slab size in powers of pages

gfpflags:
    flags like GFP_DMA for pages backing this cache

colour / colour_off:
    cache coloring info

slabp_cache:
    cache used for off-slab slab descriptors

ctor / dtor:
    object constructor / destructor

name:
    cache name visible in slabinfo
*/


/*
A useful way to think about kmem_cache:

    policy + metadata + topology for one object type
*/


/***************************************************************
 * 7. struct array_cache (PER-CPU FAST PATH)
 ***************************************************************/

/*
This is one of the most important optimizations in classic slab.

struct array_cache {
    unsigned int avail;
    unsigned int limit;
    unsigned int batchcount;
    unsigned int touched;
    spinlock_t lock;
    void *entry[0];
};

Meaning:

entry[]:
    stack-like array of objects available on that CPU

avail:
    number currently present

limit:
    maximum size of this CPU-local array

batchcount:
    how many objects to move in/out in one batch


Why this exists:
    - most alloc/free operations avoid global list locks
    - better cache warmth
    - fewer linked-list ops
    - lower SMP contention
*/


/***************************************************************
 * 8. struct kmem_list3 (PER-NODE GLOBAL BACKEND)
 ***************************************************************/

/*
Each NUMA node has a kmem_list3 for a cache.

struct kmem_list3 contains:

    slabs_partial
    slabs_full
    slabs_free

plus:
    free_objects
    free_limit
    colour_next
    list_lock
    shared
    alien

Meaning:

This is the node-local backend when CPU-local arrays are empty/full.

Allocation backend normally prefers:
    slabs_partial first

Freeing backend may move slabs between:
    full -> partial
    partial -> free
*/


/***************************************************************
 * 9. OBJECT TRACKING: PAGE -> SLAB -> CACHE
 ***************************************************************/

/*
This file uses struct page metadata to locate cache/slab for an object.

Helpers:

    page_set_cache(page, cache)
    page_get_cache(page)
    page_set_slab(page, slab)
    page_get_slab(page)

and:

    virt_to_cache(obj)
    virt_to_slab(obj)

This is how kfree()/kmem_cache_free() can discover what cache an object
belongs to.

That is a very important design point:

    object address -> page -> slab -> cache
*/


/***************************************************************
 * 10. FREELIST REPRESENTATION (BUFCTL)
 ***************************************************************/

/*
Classic slab uses kmem_bufctl_t indices to chain free objects inside a slab.

Instead of storing full pointers for each object, it often stores indexes.

Special values:

    BUFCTL_END
    BUFCTL_FREE
    BUFCTL_ACTIVE
    SLAB_LIMIT

This keeps metadata compact.
*/


/***************************************************************
 * 11. MEMORY LAYOUT OF A SLAB
 ***************************************************************/

/*
A slab occupies 2^gfporder pages.

Inside that memory there are two possible styles:

1. ON-SLAB metadata
   struct slab + bufctls live in same allocation as objects

2. OFF-SLAB metadata
   struct slab / metadata stored separately in another cache


General picture:

    [ slab metadata ][ bufctls ][ padding ][ objects ... ]

or if off-slab:

    [ objects ... only ]
    metadata elsewhere
*/


/*
Whether metadata is on-slab or off-slab matters because:
    - affects object count per slab
    - affects internal fragmentation
    - affects destruction path
*/


/***************************************************************
 * 12. CACHE CREATION BACKGROUND
 ***************************************************************/

/*
When a cache is created via kmem_cache_create(), slab.c must decide:

    - object size used internally
    - alignment
    - whether metadata is off-slab
    - order (how many pages per slab)
    - how many objects fit in one slab

This is a very important policy step.
*/


/*
Important helper logic:

    cache_estimate()
        -> given order, object size, alignment, flags
           compute number of objects and leftover bytes

    calculate_slab_order()
        -> search for acceptable page order / object count
*/


/***************************************************************
 * 13. OBJECT COUNT / SLAB ORDER CALCULATION
 ***************************************************************/

/*
The allocator tries to balance:

    - more objects per slab (good amortization)
    - low fragmentation
    - avoiding very high-order page allocations

High-order page allocations are risky because:
    - more likely to fail under fragmentation
    - expensive
    - hard on reclaim

So slab tries to avoid unnecessarily large slab orders.
*/


/*
Special note from code:
    SLAB_RECLAIM_ACCOUNT caches prefer lower order pages more strongly,
    because reclaim-related code really does not want to depend on big
    high-order allocations.
*/


/***************************************************************
 * 14. THE ALLOCATION FAST PATH
 ***************************************************************/

/*
High-level allocation flow:

    kmalloc(size)
        -> choose general kmalloc cache
        -> kmem_cache_alloc(cache)

or

    kmem_cache_alloc(custom_cache)


What slab wants first:

    ALLOCATE FROM PER-CPU array_cache
*/


/*
Fast path idea:

1. Look at this CPU's array_cache
2. If avail > 0:
       pop object from entry[]
       return immediately

No global slab list walking.
Minimal locking.
Excellent locality.
*/


/***************************************************************
 * 15. THE ALLOCATION BACKEND PATH
 ***************************************************************/

/*
If the CPU-local cache is empty, allocator goes to backend.

Backend prefers:

    partial slabs
then
    free slabs
then
    grow cache by allocating new slab pages
*/


/*
Why partial slabs first?
    Because they already contain free objects and preserve free slabs
    for future use; also helps keep object packing efficient.
*/


/***************************************************************
 * 16. GROWING A CACHE
 ***************************************************************/

/*
When no suitable objects are immediately available, slab allocates more pages.

This happens through:

    kmem_getpages(cachep, flags, nodeid)

which calls:

    alloc_pages_node(nodeid, flags, cachep->gfporder)

Then it:
    - accounts pages as slab pages
    - marks them PageSlab
    - builds slab metadata
    - initializes object freelist
    - optionally runs constructors
*/


/*
This is the key bridge:

    slab allocator OBJECT world
            ↓
      buddy allocator PAGE world
*/


/***************************************************************
 * 17. FREE PATH
 ***************************************************************/

/*
Freeing is the mirror image.

Typical free flow:

1. Find object's slab/cache from object address
2. Try to place object into this CPU's array_cache
3. If CPU array overflows, flush a batch back to global slab backend
4. Update slab state if needed
*/


/*
Again, the design goal is to make free fast in the common case by using
per-CPU arrays instead of immediately touching global lists.
*/


/***************************************************************
 * 18. SLAB STATE TRANSITIONS
 ***************************************************************/

/*
Each slab can move among these states:

    free <-> partial <-> full

Examples:

Allocation from free slab:
    free -> partial

Allocation that consumes last free object:
    partial -> full

Freeing into full slab:
    full -> partial

Freeing last allocated object:
    partial -> free
*/


/*
This tri-list organization is one of the core ideas of slab.c.
*/


/***************************************************************
 * 19. GENERAL kmalloc CACHES
 ***************************************************************/

/*
The file also sets up the general-purpose kmalloc caches.

Example sizes:
    kmalloc-32
    kmalloc-64
    kmalloc-128
    ...

Internally, malloc_sizes[] maps size classes to caches.

Function:
    __find_general_cachep(size, gfpflags)

This is how plain kmalloc() chooses its backing slab cache.
*/


/*
So:

    kmalloc(40)
        -> maybe size-64 cache

    kmalloc(100)
        -> maybe size-128 cache

This is separate from custom caches created by kmem_cache_create().
*/


/***************************************************************
 * 20. BOOTSTRAP PROBLEM (VERY IMPORTANT AND VERY INTERESTING)
 ***************************************************************/

/*
Classic chicken-and-egg problem:

    slab allocator needs memory to build its own metadata
    but those caches do not exist yet

The file comments explicitly describe bootstrap stages.

Solution used here:
    - use static bootstrap structures early
    - bring up cache_cache first
    - bring up the first kmalloc caches
    - later replace bootstrap arrays/list3s with real allocated ones
*/


/*
This is why slab initialization code is long and subtle.

The allocator must become self-hosting gradually.
*/


/***************************************************************
 * 21. cache_cache
 ***************************************************************/

/*
Very important special cache:

    cache_cache

This is the cache that stores struct kmem_cache objects themselves.

Except for cache_cache itself, every kmem_cache descriptor is usually
allocated from cache_cache.

So cache_cache is central to slab bootstrapping.
*/


/***************************************************************
 * 22. PER-CPU ARRAYS IN DETAIL
 ***************************************************************/

/*
Per-CPU arrays are strictly LIFO.

Why LIFO?
    - better cache warmth
    - recently used object likely still hot in CPU cache

When array overflows:
    roughly part of it is transferred back to global/backend lists.

When array underflows:
    batch of objects pulled from backend.
*/


/*
This gives a nice amortization effect:

    instead of taking a global lock for every alloc/free,
    we take it once per batch.
*/


/***************************************************************
 * 23. SMP SYNCHRONIZATION MODEL
 ***************************************************************/

/*
The top comments explain synchronization well.

Key points:

1. Constructors/destructors are called without allocator locking.
2. Constant fields are read locklessly.
3. Per-CPU arrays are used only by the owning CPU.
4. Non-constant shared members are protected by cache/node spinlocks.
5. cache_chain is protected by cache_chain_mutex.

Very important note from comments:
    per-cpu arrays must not be touched from the wrong CPU.
*/


/***************************************************************
 * 24. NUMA DESIGN
 ***************************************************************/

/*
In NUMA systems, each node has its own slab lists.

Comment summary:
    - each node has partial/free/full slabs
    - allocations for a node prefer node-specific slabs

This improves locality:
    CPU allocates objects from memory near its node when possible.
*/


/***************************************************************
 * 25. ALIEN CACHES (ADVANCED NUMA IDEA)
 ***************************************************************/

/*
Alien caches are used when freeing an object on a CPU/node different from
where the slab belongs.

Instead of immediately doing expensive remote list operations every time,
freeing can be staged through alien caches and drained later.

This is a NUMA optimization to reduce remote contention and keep ownership
organized by node.
*/


/*
Mental model:

    object belongs to node A
    freed on node B
        -> temporary staging
        -> later drained back toward node A backend
*/


/***************************************************************
 * 26. REAPING / SHRINKING BACKGROUND
 ***************************************************************/

/*
Slab does not want to keep unlimited free memory stuck in caches forever.

So there are timers/work items to periodically reap/drain caches.

Relevant ideas:
    - CPU-local caches can be drained
    - node lists can be reaped
    - free slabs can be returned to page allocator

The code uses delayed work and timeout heuristics.
*/


/*
Why reap?
    Because objects trapped in per-CPU caches or free slabs are memory the
    rest of the system cannot use.
*/


/***************************************************************
 * 27. DEBUG FEATURES
 ***************************************************************/

/*
When CONFIG_DEBUG_SLAB is enabled, this file supports several checks.

Key flags:

    SLAB_POISON
    SLAB_RED_ZONE
    SLAB_STORE_USER
    SLAB_DEBUG_INITIAL

These help detect:
    - use-after-free
    - buffer overruns
    - writes before/after object boundaries
    - who last touched an object
*/


/*
Redzone idea:

    [ redzone ][ object ][ redzone ]

If redzone gets overwritten, object overflow/underflow likely happened.
*/


/*
Poison idea:

    freed object filled with known byte pattern

If later reused incorrectly or corrupted, allocator can detect that.
*/


/***************************************************************
 * 28. DESTROY-BY-RCU SUPPORT
 ***************************************************************/

/*
Another advanced feature is SLAB_DESTROY_BY_RCU.

When enabled, slab pages are not immediately freed for reuse on destroy.
Instead, freeing is deferred via RCU callback.

Reason:
    Some users access objects through lockless/oblique references and need
    memory not to be immediately recycled as a different object type.

This is a very important kernel technique for safe lockless patterns.
*/


/***************************************************************
 * 29. PAGE ACCOUNTING INTERACTION
 ***************************************************************/

/*
When slab gets pages from buddy allocator:
    kmem_getpages()

it accounts them as either:
    NR_SLAB_RECLAIMABLE
or
    NR_SLAB_UNRECLAIMABLE

This matters for VM accounting and reclaim statistics.
*/


/*
So slab memory is visible to the VM as a category of memory usage.
*/


/***************************************************************
 * 30. CACHE COLORING
 ***************************************************************/

/*
Classic slab supports cache coloring.

Idea:
    slightly vary object/slab start offsets so objects from different slabs
    do not all map to the same CPU cache lines/index sets.

Fields involved:
    colour
    colour_off
    colour_next

This is an old but clever performance optimization.
*/


/***************************************************************
 * 31. ON-SLAB VS OFF-SLAB METADATA
 ***************************************************************/

/*
This distinction is important for understanding allocator geometry.

ON-SLAB:
    slab descriptor and freelist metadata live in same pages as objects

OFF-SLAB:
    metadata allocated separately from another cache

Tradeoff:
    on-slab is simpler and saves separate allocations,
    off-slab can be better for large objects where metadata would waste too
    much room inside object pages.
*/


/***************************************************************
 * 32. HIGH-LEVEL FLOW OF kmem_cache_create()
 ***************************************************************/

/*
Conceptual creation flow:

1. Validate flags and alignment
2. Compute internal object size and metadata layout
3. Decide slab order and object count
4. Set up node lists and CPU arrays
5. Link cache into global cache_chain
6. Cache becomes available for allocations
*/


/***************************************************************
 * 33. HIGH-LEVEL FLOW OF kmem_cache_alloc()
 ***************************************************************/

/*
Conceptual allocation flow:

kmem_cache_alloc(cache)
    ↓
check local CPU array_cache
    ↓
if hit:
    pop object and return
else:
    refill from backend
        ↓
    prefer partial slabs
        ↓
    else free slabs
        ↓
    else allocate new slab pages
        ↓
    return object
*/


/***************************************************************
 * 34. HIGH-LEVEL FLOW OF kmem_cache_free()
 ***************************************************************/

/*
Conceptual free flow:

kmem_cache_free(cache, obj)
    ↓
identify slab/object index
    ↓
push into CPU-local array_cache if possible
    ↓
if local array over limit:
    flush batch back to backend slab lists
    ↓
possibly move slab among full/partial/free lists
*/


/***************************************************************
 * 35. HOW kfree() WORKS CONCEPTUALLY
 ***************************************************************/

/*
For generic kmalloc allocations:

    kfree(obj)
        ↓
    virt_to_page(obj)
        ↓
    page -> slab -> cache
        ↓
    free object back into that cache

This is why page metadata linkage is so important.
*/


/***************************************************************
 * 36. WHY PARTIAL SLABS ARE THE SWEET SPOT
 ***************************************************************/

/*
The allocator strongly prefers partial slabs.

Why?

1. Already contains free objects.
2. Keeps objects concentrated rather than scattering allocations.
3. Improves chance that some slabs become completely free and returnable.
4. Helps reduce memory waste.
*/


/***************************************************************
 * 37. CPU HOTPLUG SUPPORT
 ***************************************************************/

/*
The file also contains CPU notifier logic for bringing caches online/offline
with CPU hotplug.

When CPUs appear/disappear, slab may need to:
    - allocate/free per-CPU array caches
    - set up node-local structures
    - drain objects from dead CPUs

This is one of the reasons the code is fairly intricate.
*/


/***************************************************************
 * 38. RELATION TO BUDDY ALLOCATOR
 ***************************************************************/

/*
Very important conceptual distinction:

Buddy allocator:
    allocates contiguous PAGE blocks

SLAB allocator:
    subdivides those pages into kernel objects

So slab is not replacing buddy; it is building on top of it.
*/


/*
ASCII:

    alloc_pages(order)
        -> returns raw pages
            -> slab carves into objects
                -> object returned to kernel caller
*/


/***************************************************************
 * 39. RELATION TO kmalloc()
 ***************************************************************/

/*
Plain kmalloc() is basically a convenience frontend for a family of
prebuilt slab caches.

That means:

    kmalloc(size, flags)
        -> choose nearest size cache
        -> allocate one object from that cache

Whereas kmem_cache_create() is used when subsystem wants its own dedicated
object cache with custom ctor/dtor/flags/alignment.
*/


/***************************************************************
 * 40. WHERE THIS FITS IN THE WHOLE MM STORY
 ***************************************************************/

/*
You have been reading files around:

    page-writeback.c
    pdflush.c
    page_io.c
    readahead.c
    prio_tree.c
    rmap.c
    shmem.c

Those are mostly about page cache, swap, reclaim, and mappings.

slab.c is different:
    it is about kernel heap object allocation.

So think:

    page allocator / VM world  -> pages
    slab allocator             -> kernel objects inside pages
*/


/***************************************************************
 * 41. INTERVIEW MENTAL MODEL
 ***************************************************************/

/*
If asked:

"What is slab.c doing?"

Good answer:

    The classic Linux slab allocator manages caches of fixed-size kernel
    objects on top of the page allocator. Each cache has slabs containing
    multiple objects, grouped into full/partial/free lists. Most allocations
    and frees hit per-CPU arrays for speed, while node-local slab lists handle
    the backend. The allocator supports constructors, NUMA locality, debug
    poisoning/redzones, and efficient reuse of kernel objects.
*/


/***************************************************************
 * 42. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/slab.c implements the classic Linux SLAB allocator: an object-caching
kernel memory allocator built on top of the page allocator, optimized with
per-CPU fast paths, per-node slab lists, and extensive debugging support.
*/


/***************************************************************
 * END
 ***************************************************************/
