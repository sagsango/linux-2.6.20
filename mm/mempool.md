================================================================================
FILE: linux_mempool_background_and_flow.txt
TOPIC: mempool.c - Guaranteed Reserve Allocator, Deadlock-Free Emergency
       Allocations, Pool Growth/Shrink, Blocking Allocation, and Common Helpers
SOURCE: mm/mempool.c
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHY mempool EXISTS
================================================================================

Normal kernel allocation can fail under memory pressure.

That is usually acceptable for many code paths, but some subsystems cannot
tolerate allocation failure at certain points.

Typical examples:

    block layer
    writeback paths
    low-memory recovery paths
    I/O completion or request construction paths
    code that itself helps free memory or finish I/O

The danger is this:

    subsystem needs memory
        ->
    normal allocator is under heavy pressure
        ->
    allocation fails
        ->
    subsystem cannot make forward progress
        ->
    memory reclaim / I/O completion / freeing work gets stuck
        ->
    deadlock or livelock risk

So mempool exists to provide:

    a guaranteed reserve of preallocated elements

The core idea is:

    allocate some objects ahead of time while the system is healthy,
    then under pressure, fall back to the reserve.

That is why the file comment says:

    guaranteed, deadlock-free memory allocations during extreme VM load


So mempool is an emergency reserve allocator.



SECTION 2: BIG PICTURE
================================================================================

This file implements a generic pool of preallocated elements.

A mempool does not know what its elements are.

Instead, the caller supplies:

    alloc_fn
    free_fn
    pool_data

So mempool can manage pools of:

    slab objects
    kmalloc buffers
    zeroed kmalloc buffers
    pages
    custom subsystem-specific objects

The pool tracks:

    min_nr   = guaranteed reserve size
    curr_nr  = how many reserve elements are currently sitting in pool
    elements = stack/array of reserve elements
    waitqueue for sleepers when reserve is temporarily empty

Main operations are:

    mempool_create()
    mempool_resize()
    mempool_alloc()
    mempool_free()
    mempool_destroy()

The guarantee is roughly:

    from process context, mempool_alloc() should not fail permanently,
    because if direct allocation fails, it can wait until an element is
    returned or use a reserved one.

That is the core semantic of this file.



SECTION 3: WHAT A MEMPOOL REALLY STORES
================================================================================

A mempool stores reserve objects in:

    pool->elements[]

It behaves like a simple stack.

When freeing back into the pool:

    push element into array

When allocating from reserve:

    pop element from array

The helpers are:

    add_element()
    remove_element()

So the reserve is conceptually:

    a LIFO cache of emergency objects

This is simple and efficient.



SECTION 4: add_element()
================================================================================

Function:

    static void add_element(mempool_t *pool, void *element)

Code idea:

    BUG_ON(pool->curr_nr >= pool->min_nr);
    pool->elements[pool->curr_nr++] = element;

Purpose:

    return one element to the reserve array

Important invariant:

    curr_nr must never exceed min_nr

Why?

Because mempool only reserves up to its configured guaranteed depth.
Anything above that does not need to stay in reserve and should instead be
freed back to the underlying allocator.

So add_element() is only used when the pool still needs reserve objects.



SECTION 5: remove_element()
================================================================================

Function:

    static void *remove_element(mempool_t *pool)

Code idea:

    BUG_ON(pool->curr_nr <= 0);
    return pool->elements[--pool->curr_nr];

Purpose:

    take one element out of the reserve

So reserve allocation path is:

    if reserve not empty
        pop one element
        return it

This is the emergency fallback mechanism.



SECTION 6: free_pool()
================================================================================

Function:

    static void free_pool(mempool_t *pool)

Purpose:

    destroy the entire pool and free all reserve elements plus metadata

Flow:

    while (pool->curr_nr) {
        element = remove_element(pool);
        pool->free(element, pool->pool_data);
    }

    kfree(pool->elements);
    kfree(pool);

Meaning:

    every reserve object is returned using caller-provided free function
    then the pool internals are freed

This helper is used when:

    pool creation fails midway
    or
    mempool_destroy() tears down a fully returned pool



SECTION 7: mempool_create()
================================================================================

Function:

    mempool_t *mempool_create(int min_nr,
                              mempool_alloc_t *alloc_fn,
                              mempool_free_t *free_fn,
                              void *pool_data)

Purpose:

    create a memory pool with a guaranteed minimum reserve size

Implementation:

    just calls mempool_create_node(..., -1)

So the real logic lives in:

    mempool_create_node()

This wrapper is the generic no-NUMA-specific version.



SECTION 8: mempool_create_node()
================================================================================

Function:

    mempool_t *mempool_create_node(int min_nr,
                                   mempool_alloc_t *alloc_fn,
                                   mempool_free_t *free_fn,
                                   void *pool_data,
                                   int node_id)

Purpose:

    allocate and initialize a mempool, optionally NUMA-localized

Flow:

STEP 1: allocate mempool structure

    pool = kmalloc_node(sizeof(*pool), GFP_KERNEL, node_id)
    if (!pool)
        return NULL

STEP 2: zero it

    memset(pool, 0, sizeof(*pool))

STEP 3: allocate element pointer array

    pool->elements = kmalloc_node(min_nr * sizeof(void *), GFP_KERNEL, node_id)
    if (!pool->elements) {
        kfree(pool);
        return NULL;
    }

STEP 4: initialize fields

    spin_lock_init(&pool->lock)
    pool->min_nr = min_nr
    pool->pool_data = pool_data
    init_waitqueue_head(&pool->wait)
    pool->alloc = alloc_fn
    pool->free = free_fn

STEP 5: preallocate reserve

    while (pool->curr_nr < pool->min_nr) {
        element = pool->alloc(GFP_KERNEL, pool->pool_data)
        if (!element) {
            free_pool(pool)
            return NULL
        }
        add_element(pool, element)
    }

Return fully initialized pool.

This preallocation loop is the heart of the whole design.

The pool guarantee only works because reserve objects are created upfront.



SECTION 9: WHY PREALLOCATION IS ESSENTIAL
================================================================================

Without preallocation, mempool would just be another wrapper around kmalloc or
alloc_pages and would not solve the deadlock problem.

The whole point is:

    under normal conditions:
        allocate reserve objects now

so that later under low-memory conditions:

    subsystem can still obtain an object from reserve

So the guarantee relies on:

    min_nr reserve already existing before crisis happens

That is why creation may sleep and may take time, but later emergency use is
reliable.



SECTION 10: mempool_resize()
================================================================================

Function:

    int mempool_resize(mempool_t *pool, int new_min_nr, gfp_t gfp_mask)

Purpose:

    change the guaranteed reserve size of an existing pool

It supports both:

    shrinking
    growing

Important note from comment:

    caller must ensure mempool_destroy is not racing
    mempool_alloc/free may still run concurrently

So resize must be careful with locking and races.



SECTION 11: SHRINK PATH IN mempool_resize()
================================================================================

If:

    new_min_nr <= pool->min_nr

Then shrink logic runs.

Flow:

    spin_lock_irqsave(&pool->lock, flags)

    while (new_min_nr < pool->curr_nr) {
        element = remove_element(pool)
        spin_unlock_irqrestore(...)
        pool->free(element, pool->pool_data)
        spin_lock_irqsave(...)
    }

    pool->min_nr = new_min_nr

Meaning:

    if reserve currently holds more elements than new minimum,
    throw excess elements back to underlying allocator

This shrink only affects currently stored reserve elements.
Outstanding checked-out objects are not forcibly reclaimed.



SECTION 12: GROW PATH IN mempool_resize()
================================================================================

If:

    new_min_nr > pool->min_nr

Then grow logic runs.

Flow overview:

1) allocate a larger elements[] pointer array

    new_elements = kmalloc(new_min_nr * sizeof(*new_elements), gfp_mask)

2) take lock and recheck race

    if another resizer already grew/shrank enough, just free temporary array

3) replace pool->elements with new array
4) update pool->min_nr
5) allocate more reserve objects until curr_nr reaches min_nr

Important race handling:

After allocating a new element outside the lock:

    if pool->curr_nr < pool->min_nr
        add_element(pool, element)
    else
        free it because another thread raced and already satisfied pool

So resize is race-aware and lock-efficient:
    metadata replacement under lock
    expensive allocations mostly outside lock



SECTION 13: WHY mempool_resize() MAY NOT FULLY GROW IMMEDIATELY
================================================================================

The comment says:

    In the case of growing, it cannot be guaranteed that the pool will be
    grown to the new size immediately, but new mempool_free() calls will refill it.

This is because the underlying alloc_fn may fail under pressure.

So after increasing min_nr:

    pool knows it wants a bigger reserve
    but may not be able to fill all missing reserve elements right away

Later, when users free elements back:

    mempool_free() will preferentially refill reserve until curr_nr == min_nr

So growth target can be reached gradually over time.



SECTION 14: mempool_destroy()
================================================================================

Function:

    void mempool_destroy(mempool_t *pool)

Purpose:

    destroy a mempool once caller has returned all outstanding elements

Critical check:

    BUG_ON(pool->curr_nr != pool->min_nr);

This means:

    every object taken from pool must have been returned

Why this check?

Suppose min_nr = 64.
If curr_nr is less than min_nr at destroy time, that means some objects are
still checked out by callers.

Destroying the pool then would leak objects or free metadata while users still
hold pooled objects.

So destroy requires:

    no outstanding borrowed elements

Then:

    free_pool(pool)



SECTION 15: mempool_alloc() BACKGROUND
================================================================================

Function:

    void *mempool_alloc(mempool_t *pool, gfp_t gfp_mask)

Purpose:

    allocate one element from the mempool

This is the most important runtime function.

Its strategy is:

    first try direct allocation from underlying allocator
    if that fails, use reserve
    if reserve empty and caller may sleep, wait until someone frees one
    then retry

This is what gives mempool its deadlock-resistant behavior.



SECTION 16: GFP FLAG ADJUSTMENTS IN mempool_alloc()
================================================================================

The function modifies GFP behavior:

    gfp_mask |= __GFP_NOMEMALLOC
    gfp_mask |= __GFP_NORETRY
    gfp_mask |= __GFP_NOWARN

Meaning:

__GFP_NOMEMALLOC
    do not dip into allocator emergency reserves

Why?
    mempool itself is the subsystem-level emergency reserve.
    It should not consume global emergency reserves first.

__GFP_NORETRY
    do not loop too aggressively in page allocator

Why?
    if direct allocation is failing, mempool will fallback to its own reserve
    or block waiting, rather than thrash reclaim excessively

__GFP_NOWARN
    allocation failures are expected/acceptable here

Why?
    failing direct alloc is part of normal mempool behavior under pressure


Then initially:

    gfp_temp = gfp_mask & ~(__GFP_WAIT|__GFP_IO)

So first attempt avoids sleeping and I/O-heavy reclaim.



SECTION 17: FAST PATH OF mempool_alloc()
================================================================================

Flow starts at label:

    repeat_alloc:

STEP 1: try direct underlying allocation first

    element = pool->alloc(gfp_temp, pool->pool_data)
    if (likely(element != NULL))
        return element

This is important.

Even if pool exists, normal allocation is preferred first.

Why?

Because reserve should be preserved for true emergencies.
If allocator still succeeds, there is no reason to consume reserve.



SECTION 18: RESERVE FALLBACK PATH OF mempool_alloc()
================================================================================

If direct allocation fails:

    spin_lock_irqsave(&pool->lock, flags)
    if (likely(pool->curr_nr)) {
        element = remove_element(pool)
        spin_unlock_irqrestore(...)
        return element
    }
    spin_unlock_irqrestore(...)

So second choice is:

    borrow one preallocated reserve element

This is the key emergency path.

Reserve is consumed only after direct allocation failed.



SECTION 19: ATOMIC / NON-SLEEPING FAILURE CASE
================================================================================

If reserve is empty too:

    if (!(gfp_mask & __GFP_WAIT))
        return NULL

Meaning:

    callers that cannot sleep are not guaranteed success
    if both direct allocation and reserve fail, allocation fails

This matches the comment:

    it might fail if called from IRQ context

So the strong guarantee is mainly for:

    process context
    sleepable allocation context

not strict atomic contexts.



SECTION 20: BLOCKING WAIT PATH OF mempool_alloc()
================================================================================

If caller may sleep:

    gfp_temp = gfp_mask
    init_wait(&wait)
    prepare_to_wait(&pool->wait, &wait, TASK_UNINTERRUPTIBLE)
    smp_mb()
    if (!pool->curr_nr)
        io_schedule_timeout(5*HZ)
    finish_wait(&pool->wait, &wait)
    goto repeat_alloc

Meaning:

    once reserve is empty, caller goes to sleep waiting for some other user
    to return an element via mempool_free()

Then after wakeup:

    retry from scratch
        -> try direct alloc again
        -> then reserve again

Why retry direct alloc again first?

Because conditions may have improved; reserve should still be preserved when
possible.



SECTION 21: WHY THIS HELPS AVOID DEADLOCK
================================================================================

Imagine a subsystem like block I/O request handling:

    all workers need some small object to complete I/O
    memory pressure makes kmalloc fail

Without mempool:

    no object -> no I/O completion -> no freed memory -> deadlock risk

With mempool:

    reserve object exists
    or blocked waiter sleeps until another object is returned
    I/O/completion path can still make forward progress

That is the main design goal.

The pool turns a hard immediate failure into:

    reserve use
or
    controlled wait for object recycling

This is why mempool is commonly used in block layer and reclaim-sensitive paths.



SECTION 22: mempool_free()
================================================================================

Function:

    void mempool_free(void *element, mempool_t *pool)

Purpose:

    return an object either to reserve pool or to underlying allocator

This is symmetric to alloc.

Flow:

    smp_mb()
    if (pool->curr_nr < pool->min_nr) {
        spin_lock_irqsave(&pool->lock, flags)
        if (pool->curr_nr < pool->min_nr) {
            add_element(pool, element)
            spin_unlock_irqrestore(...)
            wake_up(&pool->wait)
            return
        }
        spin_unlock_irqrestore(...)
    }
    pool->free(element, pool->pool_data)

Meaning:

Case A:
    reserve is below target min_nr
        -> refill reserve by storing element back in pool
        -> wake sleeping allocators

Case B:
    reserve is already full enough
        -> free element to underlying allocator

This is very important:

    mempool does not hoard unlimited objects
    it only keeps up to min_nr reserve
    excess objects go back to normal system allocator



SECTION 23: WHY mempool_free() WAKES WAITERS
================================================================================

When an allocation sleeper is waiting in:

    mempool_alloc()

it waits on:

    pool->wait

If mempool_free() returns an element back into reserve, that may satisfy one of
those waiting allocators.

So after adding element to reserve:

    wake_up(&pool->wait)

This creates the handoff pattern:

    one thread frees element
        ->
    sleeping allocator wakes up
        ->
    retries and may remove that reserve element

That is how pool recycling works under sustained pressure.



SECTION 24: MEMORY BARRIERS IN alloc/free
================================================================================

You see:

    smp_mb();

in both alloc/free paths around wait/refill logic.

These barriers are there to make the ordering around:

    checking curr_nr
    going to sleep
    freeing back and waking up

safe on SMP systems.

Conceptually the barriers help ensure:

    if a waiter goes to sleep because it sees no reserve,
    and another CPU frees and wakes,
    the ordering of pool state and wakeups is visible correctly.

The exact low-level memory ordering details are architecture-specific, but the
high-level point is:

    wait/wakeup and pool state transitions must be synchronized safely.



SECTION 25: COMMON GENERIC HELPER CALLBACKS
================================================================================

This file also provides ready-made alloc/free helpers so users do not need to
write custom callbacks for common cases.

These helpers make mempool generic and reusable.



SECTION 26: mempool_alloc_slab() / mempool_free_slab()
================================================================================

Functions:

    void *mempool_alloc_slab(gfp_t gfp_mask, void *pool_data)
    void mempool_free_slab(void *element, void *pool_data)

Meaning:

    pool_data is a struct kmem_cache *
    allocate/free one slab object from that cache

Implementation:

    kmem_cache_alloc(mem, gfp_mask)
    kmem_cache_free(mem, element)

So this is for pools of slab objects.



SECTION 27: mempool_kmalloc() / mempool_kzalloc() / mempool_kfree()
================================================================================

Functions:

    void *mempool_kmalloc(gfp_t gfp_mask, void *pool_data)
    void *mempool_kzalloc(gfp_t gfp_mask, void *pool_data)
    void mempool_kfree(void *element, void *pool_data)

Meaning:

    pool_data encodes object size

mempool_kmalloc:
    allocate size bytes with kmalloc

mempool_kzalloc:
    allocate zeroed size bytes with kzalloc

mempool_kfree:
    free with kfree

So this is for pools of ordinary heap buffers.



SECTION 28: mempool_alloc_pages() / mempool_free_pages()
================================================================================

Functions:

    void *mempool_alloc_pages(gfp_t gfp_mask, void *pool_data)
    void mempool_free_pages(void *element, void *pool_data)

Meaning:

    pool_data encodes page order

alloc:
    alloc_pages(gfp_mask, order)

free:
    __free_pages(element, order)

So mempool can also reserve pages, not just objects.

This is heavily useful for things like:

    bounce buffers
    emergency page pools
    block I/O page reserves



SECTION 29: COMPLETE CREATE / ALLOC / FREE / DESTROY FLOW
================================================================================

Creation:
    mempool_create(min_nr, alloc_fn, free_fn, pool_data)
        |
        +--> allocate mempool structure
        +--> allocate elements[] array
        +--> initialize lock, waitqueue, callbacks, min_nr
        +--> preallocate min_nr reserve objects using alloc_fn
        v
    ready pool

Allocation:
    mempool_alloc(pool, gfp_mask)
        |
        +--> try direct alloc_fn(nonblocking-ish gfp)
        |       if success: return object
        |
        +--> if reserve curr_nr > 0:
        |       pop reserve object and return
        |
        +--> if caller cannot wait:
        |       return NULL
        |
        +--> sleep on pool->wait
        +--> retry
        v
    eventually gets object

Free:
    mempool_free(element, pool)
        |
        +--> if reserve below min_nr:
        |       push object back into reserve
        |       wake waiters
        |
        +--> else:
                free_fn(element)
        v
    reserve refilled or object released

Destroy:
    mempool_destroy(pool)
        |
        +--> require curr_nr == min_nr
        +--> free all reserve elements via free_fn
        +--> free metadata
        v
    pool gone



SECTION 30: WHY DIRECT ALLOCATION IS TRIED BEFORE RESERVE
================================================================================

This is a subtle but important design choice.

Suppose pool min_nr = 64.

If every caller always consumed reserve first, then reserve would be depleted
even when system allocator is healthy.

Then when real emergency happens:

    reserve already gone

So mempool_alloc() prefers:

    direct alloc first

and only if that fails:

    reserve fallback

Thus the reserve is conserved for actual low-memory situations.



SECTION 31: WHY POOL SIZE IS A GUARANTEE, NOT A CACHE SIZE
================================================================================

min_nr is not just "a convenient cache depth".

It is meant to be:

    minimum number of elements that the subsystem can rely on as an emergency
    reserve target

This is why free path tries to refill until:

    curr_nr == min_nr

So mempool is not optimizing for reuse locality first.
It is optimizing for:

    reserve availability under pressure



SECTION 32: PRACTICAL MENTAL MODEL
================================================================================

Think of mempool like a hospital emergency supply cabinet.

Normal day:

    if needed item is available from central warehouse,
    use warehouse supply

Emergency day:

    if warehouse is failing or overloaded,
    open emergency cabinet

If even cabinet is empty:

    in non-urgent cases wait until another item is returned/restocked
    in atomic/no-wait case, fail

When supplies are returned:

    refill emergency cabinet first until its guaranteed stock level is restored
    only extra supplies go back to central warehouse

That is exactly how mempool behaves relative to the normal allocator.



SECTION 33: SUMMARY
================================================================================

mm/mempool.c implements a generic preallocated reserve allocator for kernel
subsystems that need reliable progress under severe memory pressure.

Main ideas:

    keep a preallocated reserve of min_nr elements
    use direct allocation first when possible
    fall back to reserve when direct allocation fails
    sleep and wait for returned elements in sleepable contexts
    refill reserve first on free until guarantee is restored

Key functions:

    mempool_create()
    mempool_create_node()
        create and prefill reserve pool

    mempool_resize()
        shrink or grow pool target reserve size

    mempool_alloc()
        direct allocate -> reserve fallback -> wait/retry

    mempool_free()
        refill reserve if below min_nr, otherwise free normally

    mempool_destroy()
        destroy pool after all outstanding elements are returned

Common helpers:

    mempool_alloc_slab() / mempool_free_slab()
    mempool_kmalloc() / mempool_kzalloc() / mempool_kfree()
    mempool_alloc_pages() / mempool_free_pages()

Core guarantee:

    for sleepable process-context callers, mempool_alloc is designed so that
    allocation can still make forward progress even under extreme VM pressure,
    because a preallocated reserve exists and sleepers can wait for reserve
    recycling.

In short:

    mempool is Linux's subsystem-level emergency allocation reserve, built to
    prevent deadlocks and allocation starvation in critical reclaim and I/O
    paths.


================================================================================
END OF FILE
================================================================================
