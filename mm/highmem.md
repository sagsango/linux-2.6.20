================================================================================
FILE: linux_highmem_pkmap_background_and_flow.txt
TOPIC: High Memory Mapping, pkmap, and page->virtual Tracking
SOURCE: mm/highmem.c (pkmap / page_address portion)
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHY THIS CODE EXISTS
================================================================================

This code exists because on 32-bit Linux, the kernel cannot permanently map
all physical RAM into its virtual address space when the machine has a lot
of RAM.

On 32-bit x86, kernel virtual address space is limited.
A typical split is something like:

    user virtual space     : lower 3 GB
    kernel virtual space   : upper 1 GB

Inside that kernel 1 GB, a lot is already consumed by:

    kernel text/data
    vmalloc
    fixmaps
    direct lowmem mapping
    other kernel regions

So if the machine has more physical RAM than can fit in the permanent kernel
linear mapping, some RAM pages are placed in:

    HIGHMEM

These pages exist in physical memory,
but they do NOT have a permanent kernel virtual address.

That creates the problem:

    kernel code often needs a kernel virtual address to access page contents

Examples:

    copy to/from page cache
    block I/O bounce handling
    filesystem operations
    memcpy into/out of a page
    page zeroing
    bio completion paths

So the kernel needs a way to temporarily map a highmem page into kernel
virtual address space.

This file implements that mechanism.



SECTION 2: LOWMEM VS HIGHMEM
================================================================================

LOWMEM pages:

    permanently mapped into kernel virtual space
    kernel can do:

        page_address(page)

    and get a stable direct virtual address


HIGHMEM pages:

    not permanently mapped
    kernel cannot always directly dereference them
    must first create a temporary kernel mapping


So:

    lowmem page   -> always directly addressable
    highmem page  -> needs kmap()/kunmap() or kmap_atomic()


This source is about the common/schedulable highmem mapping path.



SECTION 3: BIG PICTURE
================================================================================

This file implements two major pieces:

A) PKMAP permanent-temporary mappings

    These are the global kernel mapping slots used by:

        kmap_high()
        kunmap_high()

    They allow a highmem page to be mapped into a special kernel virtual area.

B) page -> virtual lookup support

    For highmem pages, page_address(page) cannot simply use the normal direct map.
    So this file keeps a separate association table saying:

        this highmem struct page currently maps to this kernel virtual address


So the file answers two questions:

    1) How do we create/reuse a kernel virtual mapping for a highmem page?
    2) How do we find the current virtual address of a highmem page?



SECTION 4: WHAT "PERMANENT KMAP" MEANS HERE
================================================================================

The comment says:

    Implemented permanent (schedulable) kmaps

This does NOT mean:

    mapped forever for all time

It means:

    mapping can survive across schedule()
    mapping is globally visible in a special pkmap region
    unlike atomic kmap, it may sleep/block

So kmap_high() is the "sleepable/global" mapping path.

This is different from:

    kmap_atomic()

which is short-lived, CPU-local-ish in spirit, and cannot sleep.



SECTION 5: VIRTUAL_COUNT COMMENT - VERY IMPORTANT
================================================================================

At the top:

    /*
     * Virtual_count is not a pure "count".
     *  0 means that it is not mapped, and has not been mapped
     *    since a TLB flush - it is usable.
     *  1 means that there are no users, but it has been mapped
     *    since the last TLB flush - so we can't use it.
     *  n means that there are (n-1) current users of it.
     */

This is the meaning of:

    pkmap_count[i]

This is the single most important idea in this code.

Interpretation:

    pkmap_count[i] == 0
        slot completely free and reusable

    pkmap_count[i] == 1
        no current users, BUT stale TLB mappings may still exist
        so slot cannot be reused until global flush

    pkmap_count[i] >= 2
        active users exist
        actual number of users = pkmap_count[i] - 1


So this array is not a normal reference count.
It is really:

    reference count + "TLB needs flush before reuse" state



SECTION 6: GLOBAL STATE
================================================================================

Important globals in CONFIG_HIGHMEM section:

    unsigned long totalhigh_pages

        total number of highmem pages in system


    static int pkmap_count[LAST_PKMAP]

        state/reference array for each pkmap slot


    static unsigned int last_pkmap_nr

        last slot index used, for round-robin slot search


    static DEFINE_SPINLOCK(kmap_lock)

        protects pkmap_count, slot allocation/reuse, and page_address assignment


    pte_t *pkmap_page_table

        PTE array backing the pkmap virtual region


    static DECLARE_WAIT_QUEUE_HEAD(pkmap_map_wait)

        wait queue for tasks waiting for a free pkmap slot


So:

    pkmap region = fixed virtual range
    pkmap_page_table = PTEs for that region
    pkmap_count[] = slot usage state
    kmap_lock = serialization
    pkmap_map_wait = sleep queue when region is full



SECTION 7: nr_free_highpages()
================================================================================

Function:

    unsigned int nr_free_highpages(void)

Purpose:

    count free pages in ZONE_HIGHMEM across all online nodes

Flow:

    for_each_online_pgdat(pgdat)
        pages += pgdat->node_zones[ZONE_HIGHMEM].free_pages

Meaning:

    this is just a statistics/helper function,
    not directly about pkmap slot handling

It tells how many highmem pages are free in allocator terms.



SECTION 8: WHAT IS PKMAP?
================================================================================

PKMAP is a special kernel virtual region reserved for highmem mappings.

Conceptually:

    physical highmem page
        ↕ temporary association
    PKMAP virtual slot

Macros like:

    PKMAP_ADDR(n)
    PKMAP_NR(vaddr)

convert between:

    slot number
    virtual address in pkmap region

So the system has:

    LAST_PKMAP slots

Each slot can temporarily map one highmem page.

Because the pkmap region is limited, only a limited number of highmem pages
can be simultaneously kmap_high()-mapped.



SECTION 9: WHY A GLOBAL PKMAP REGION IS NEEDED
================================================================================

Why not just permanently map every highmem page?

Because the entire point of HIGHMEM is:

    kernel virtual address space is too small to map all physical RAM permanently

So Linux reserves only a modest virtual window:

    pkmap region

and dynamically rotates highmem pages through it.

This gives the kernel:

    temporary access to arbitrary highmem pages

without needing:

    permanent virtual space for all of RAM



SECTION 10: flush_all_zero_pkmaps() BACKGROUND
================================================================================

Function:

    static void flush_all_zero_pkmaps(void)

Purpose:

    recycle pkmap slots whose count is exactly 1

Remember the meaning:

    pkmap_count == 1
        no active users
        but mapping still exists and cannot be reused until TLB flush

So flush_all_zero_pkmaps() does:

    identify all such stale-but-unused slots
    clear their PTEs
    remove page->virtual association
    then flush TLB for entire pkmap range

After that:

    those slots become pkmap_count == 0
    and are reusable



SECTION 11: flush_all_zero_pkmaps() STEP-BY-STEP
================================================================================

STEP 1: Flush cache for kmaps

    flush_cache_kmaps();

This handles architecture cache concerns before changing mappings.


STEP 2: Iterate all pkmap slots

    for (i = 0; i < LAST_PKMAP; i++) { ... }

For each slot:

    if (pkmap_count[i] != 1)
        continue;

Only slots with count 1 are eligible for recycling.


STEP 3: Mark slot fully free

    pkmap_count[i] = 0;


STEP 4: Sanity check PTE exists

    BUG_ON(pte_none(pkmap_page_table[i]));

Because a count-1 slot should still have a mapped PTE.


STEP 5: Get page currently mapped by that PTE

    page = pte_page(pkmap_page_table[i]);


STEP 6: Clear the PTE

    pte_clear(&init_mm, (unsigned long)page_address(page),
              &pkmap_page_table[i]);

This removes the pkmap virtual mapping.


STEP 7: Remove page->virtual association

    set_page_address(page, NULL);

So now the highmem page no longer reports a pkmap virtual address.


STEP 8: Flush TLB for pkmap region

    flush_tlb_kernel_range(PKMAP_ADDR(0), PKMAP_ADDR(LAST_PKMAP));

This is the crucial step.

Only after TLB flush is it safe to reuse those virtual slots,
because CPUs may have cached old translations.


So count==1 means:
    "logically unused, but TLB still dirty"

flush_all_zero_pkmaps() transitions these to:
    "fully unused and reusable"



SECTION 12: WHY COUNT MUST NOT GO DIRECTLY TO ZERO ON kunmap
================================================================================

The big rule in this subsystem is:

    a slot must not go directly from active use to reusable
    until a TLB flush has happened

Why?

Because even after all kernel code stopped "using" the mapping,
the CPU TLB might still have the virtual->physical translation cached.

If the kernel reuses that same virtual slot immediately for a DIFFERENT page,
stale TLB entries could make accesses hit the wrong physical page.

So the state machine is:

    active users       -> pkmap_count >= 2
    last user leaves   -> pkmap_count becomes 1
    global flush       -> pkmap_count becomes 0

That delayed transition is the heart of pkmap correctness.



SECTION 13: map_new_virtual() BACKGROUND
================================================================================

Function:

    static inline unsigned long map_new_virtual(struct page *page)

Purpose:

    find or create a pkmap slot for a highmem page that currently has no
    virtual mapping

This function:

    1) searches for a reusable slot
    2) if needed flushes stale slots
    3) if no slots available, sleeps waiting for one
    4) installs the PTE for the page
    5) sets page->virtual association
    6) returns virtual address



SECTION 14: map_new_virtual() STEP-BY-STEP
================================================================================

STEP 1: Start search

    count = LAST_PKMAP;

The code does a round-robin scan using:

    last_pkmap_nr = (last_pkmap_nr + 1) & LAST_PKMAP_MASK;


STEP 2: Wraparound triggers stale-slot cleanup

    if (!last_pkmap_nr) {
        flush_all_zero_pkmaps();
        count = LAST_PKMAP;
    }

Meaning:

    every full cycle through slots, recycle count==1 slots


STEP 3: Look for free slot

    if (!pkmap_count[last_pkmap_nr])
        break;

A slot is usable only when count == 0.


STEP 4: If all slots exhausted, sleep

If scan finds no reusable slot after full pass:

    add_wait_queue(&pkmap_map_wait, &wait);
    spin_unlock(&kmap_lock);
    schedule();
    remove_wait_queue(...)
    spin_lock(&kmap_lock);

This waits for some other task to unmap a slot.


STEP 5: Recheck race after sleep

    if (page_address(page))
        return (unsigned long)page_address(page);

Meaning:

    while we slept, another CPU/task may already have mapped the same page

If so:
    reuse that mapping instead of allocating a new one


STEP 6: Once free slot found, compute vaddr

    vaddr = PKMAP_ADDR(last_pkmap_nr);


STEP 7: Install PTE into pkmap page table

    set_pte_at(&init_mm, vaddr,
               &(pkmap_page_table[last_pkmap_nr]),
               mk_pte(page, kmap_prot));

This maps the highmem page into the pkmap virtual slot.


STEP 8: Initialize slot state

    pkmap_count[last_pkmap_nr] = 1;

Important:
    count starts at 1, not 0 or 2

Because:
    it is mapped, but caller hasn't yet incremented "active user" count


STEP 9: Record reverse association

    set_page_address(page, (void *)vaddr);

Now page_address(page) for this highmem page can find the mapping.


STEP 10: Return vaddr



SECTION 15: WHY map_new_virtual() SETS COUNT TO 1
================================================================================

This often confuses people.

Why not set count to 2 immediately?

Because map_new_virtual() is just:

    establish mapping in slot

Then the caller kmap_high() does:

    pkmap_count[PKMAP_NR(vaddr)]++;

So lifecycle becomes:

    new slot mapped      -> count = 1
    first active user    -> count = 2
    second active user   -> count = 3
    ...

This preserves the special meaning:

    count - 1 = active users

while reserving 1 as:

    mapped but no active users / waiting for flush semantics



SECTION 16: kmap_high() BACKGROUND
================================================================================

Function:

    void *kmap_high(struct page *page)

Purpose:

    return a stable kernel virtual address for a highmem page

This is the main slow/sleepable highmem mapping function.

It may:

    take lock
    allocate/reuse pkmap slot
    sleep waiting for slot
    increment slot use count

The comment says:

    cannot call this from interrupts, as it may block

That is because it may sleep in map_new_virtual() when no slots are free.



SECTION 17: kmap_high() STEP-BY-STEP
================================================================================

STEP 1: Take global lock

    spin_lock(&kmap_lock);

This serializes slot allocation and page->virtual lookup.


STEP 2: Check whether page already has a mapping

    vaddr = (unsigned long)page_address(page);

If already mapped:
    reuse that mapping

Else:
    vaddr = map_new_virtual(page);


STEP 3: Increment slot count

    pkmap_count[PKMAP_NR(vaddr)]++;

    BUG_ON(pkmap_count[PKMAP_NR(vaddr)] < 2);

This turns:
    newly mapped slot from 1 -> 2
or
    reused mapping from n -> n+1


STEP 4: Unlock and return

    spin_unlock(&kmap_lock);
    return (void *)vaddr;


So kmap_high() either:

    reuses an existing pkmap mapping for the page
or
    creates a new one

then accounts one active user.



SECTION 18: WHY page_address(page) IS CHECKED FIRST IN kmap_high()
================================================================================

Suppose two callers want the same highmem page mapped.

Without reuse, they might waste two pkmap slots for the same page.

Instead the design is:

    if page already mapped in pkmap region,
    just share that virtual address and increment use count

So multiple users of the same highmem page can share one pkmap slot.

That is why:

    page_address(page)

is consulted under lock before mapping a new slot.



SECTION 19: kunmap_high() BACKGROUND
================================================================================

Function:

    void kunmap_high(struct page *page)

Purpose:

    drop one active user of a highmem pkmap mapping

It does NOT necessarily clear the mapping immediately.

Instead it decrements pkmap_count and may wake waiters if the count becomes 1.

Remember:

    count==1 means no active users, but slot still cannot be reused until flush



SECTION 20: kunmap_high() STEP-BY-STEP
================================================================================

STEP 1: Lock

    spin_lock(&kmap_lock);


STEP 2: Find page's current virtual address

    vaddr = (unsigned long)page_address(page);
    BUG_ON(!vaddr);

Then compute slot number:

    nr = PKMAP_NR(vaddr);


STEP 3: Decrement slot count

    switch (--pkmap_count[nr]) { ... }

Cases:

CASE 0:
    BUG()

Because count must never fall to zero directly on kunmap.
That would violate "no zero without TLB flush" rule.

CASE 1:
    no more active users
    slot is now stale-but-unreusable
    maybe wake sleepers waiting for free slots


STEP 4: Optional wakeup optimization

    need_wakeup = waitqueue_active(&pkmap_map_wait);

Only if queue is active do we later call wake_up().


STEP 5: Unlock

    spin_unlock(&kmap_lock);


STEP 6: Wake outside spinlock if needed

    if (need_wakeup)
        wake_up(&pkmap_map_wait);

Why outside lock?
    cleaner and race-safe wakeup pattern



SECTION 21: WHY WAITERS ARE WOKEN WHEN COUNT BECOMES 1
================================================================================

A waiter in map_new_virtual() needs a slot to eventually become reusable.

A slot becomes fully reusable only after flush_all_zero_pkmaps() turns:

    count 1 -> count 0

But if no one wakes sleepers when counts drop from active to stale state,
they may sleep forever.

So when count becomes 1:

    this is a signal that progress happened
    a future flush pass may free more slots

Hence wakeup is appropriate.



SECTION 22: HASHED PAGE_VIRTUAL SUPPORT - BACKGROUND
================================================================================

Below the CONFIG_HIGHMEM block we have:

#if defined(HASHED_PAGE_VIRTUAL)

This is support for:

    page_address(page)
    set_page_address(page, virtual)

for highmem pages, using a hash table rather than a simple field.

Why?

Lowmem pages can often use a direct calculation:

    lowmem_page_address(page)

But highmem pages do not have a permanent direct mapping.

So Linux needs a separate associative structure that says:

    page X -> current temporary virtual address Y

This section implements that association table.



SECTION 23: struct page_address_map AND GLOBAL TABLES
================================================================================

Structures:

    struct page_address_map {
        struct page *page;
        void *virtual;
        struct list_head list;
    };

This represents one page->virtual association.

Global data:

    static struct list_head page_address_pool;
    static spinlock_t pool_lock;

        pool of free page_address_map entries

    static struct page_address_slot page_address_htable[1<<PA_HASH_ORDER];

Each bucket has:

    list of active mappings
    spinlock protecting that list

Static storage:

    static struct page_address_map page_address_maps[LAST_PKMAP];

Meaning:

    there are as many association records as pkmap slots

This makes sense because at most LAST_PKMAP highmem pages can be pkmap-mapped
at once.



SECTION 24: page_slot()
================================================================================

Function:

    static struct page_address_slot *page_slot(struct page *page)

Purpose:

    choose a hash-table bucket for a given page

Implementation:

    return &page_address_htable[hash_ptr(page, PA_HASH_ORDER)];

So highmem page->virtual associations are stored in:

    hashed buckets keyed by struct page pointer



SECTION 25: page_address()
================================================================================

Function:

    void *page_address(struct page *page)

Purpose:

    return kernel virtual address of a page if it currently has one

Behavior:

If page is NOT highmem:

    return lowmem_page_address(page);

So lowmem is easy/direct.

If page IS highmem:

    find hash bucket
    lock bucket
    scan list for matching page
    return pam->virtual if found
    else NULL

So for highmem:

    page_address(page) may return NULL
    if page is not currently mapped in pkmap space

That is a crucial difference from lowmem semantics.



SECTION 26: set_page_address()
================================================================================

Function:

    void set_page_address(struct page *page, void *virtual)

Purpose:

    add or remove a highmem page->virtual association

Case A: virtual != NULL  (Add association)

    BUG_ON(list_empty(&page_address_pool));

    take one free page_address_map from pool
    fill:
        pam->page = page;
        pam->virtual = virtual
    hash page into bucket
    add pam to bucket list

Case B: virtual == NULL  (Remove association)

    find matching pam in bucket list
    remove it
    return it to free pool

So this is the reverse-mapping database for pkmap slots.

Whenever a pkmap slot is created or destroyed, this table is updated.



SECTION 27: page_address_init()
================================================================================

Function:

    void __init page_address_init(void)

Purpose:

    initialize the page->virtual association subsystem

Flow:

    INIT_LIST_HEAD(&page_address_pool);

    for each page_address_maps[i]:
        add to free pool

    for each hash bucket:
        INIT_LIST_HEAD(bucket list)
        spin_lock_init(bucket lock)

    spin_lock_init(&pool_lock);

So after boot:

    all mapping descriptors are free
    all buckets are empty
    system is ready to record highmem page mappings



SECTION 28: HOW PKMAP AND PAGE_ADDRESS HASHING FIT TOGETHER
================================================================================

The full relationship is:

When a highmem page gets mapped in pkmap:

    map_new_virtual(page)
        |
        +--> choose pkmap slot
        +--> install PTE in pkmap_page_table
        +--> pkmap_count[slot] = 1
        +--> set_page_address(page, vaddr)

So now:

    page_address(page) -> vaddr

When kmap_high(page) is called again:

    page_address(page) can find existing mapping
    so same slot is reused

When stale slot is finally reclaimed:

    flush_all_zero_pkmaps()
        |
        +--> pte_clear(...)
        +--> set_page_address(page, NULL)

So page->virtual association disappears.



SECTION 29: COMPLETE HIGHMEM MAPPING FLOW
================================================================================

Highmem page needs kernel access
    |
    v
kmap_high(page)
    |
    +--> lock kmap_lock
    +--> page_address(page)?
            |
            +--> yes:
            |       reuse existing vaddr
            |
            +--> no:
                    map_new_virtual(page)
                        |
                        +--> scan pkmap slots
                        +--> maybe flush stale count==1 slots
                        +--> maybe sleep if full
                        +--> install PTE in pkmap region
                        +--> set_page_address(page, vaddr)
                        +--> pkmap_count[slot] = 1
    |
    +--> pkmap_count[slot]++
    |
    +--> unlock
    |
    v
caller uses returned vaddr
    |
    v
kunmap_high(page)
    |
    +--> lock kmap_lock
    +--> find slot via page_address(page)
    +--> decrement pkmap_count
    +--> if count becomes 1, maybe wake sleepers
    +--> unlock
    |
    v
later, on slot recycling pass:
    flush_all_zero_pkmaps()
        |
        +--> for each slot with count==1:
                clear PTE
                set_page_address(page, NULL)
                pkmap_count = 0
        +--> flush pkmap TLB range



SECTION 30: WHY THIS DESIGN WORKS
================================================================================

This design solves the 32-bit highmem problem by separating:

    physical page existence
from
    kernel virtual accessibility

A highmem page always exists physically,
but kernel virtual access is only created on demand.

The design is efficient because:

    mappings are reused if same page already mapped
    only a limited virtual window is reserved
    inactive stale mappings are lazily reclaimed in batches
    TLB flush cost is amortized

The design is safe because:

    slot reuse never happens before TLB flush
    page->virtual association is protected by locks
    page use counts distinguish active users from stale mappings



SECTION 31: LIMITATIONS / COSTS
================================================================================

This mechanism has costs:

    limited number of pkmap slots
    may sleep waiting for a free slot
    global lock contention on kmap_lock
    TLB flushes across entire pkmap region
    extra page->virtual bookkeeping

That is why later kernels and 64-bit systems rely less on this exact model.

But for 32-bit highmem, this was essential.



SECTION 32: SIMPLE MENTAL MODEL
================================================================================

Think of HIGHMEM as books stored in a warehouse outside the library room.

LOWMEM books:

    already on shelves in the room
    immediately readable

HIGHMEM books:

    in storage outside
    must be brought to a temporary reading desk

PKMAP region:

    the set of temporary reading desks

kmap_high(page):

    assign a desk to this book and let user read it

kunmap_high(page):

    user leaves the desk

pkmap_count == 1:

    desk is empty now, but we have not yet cleared old room-directory entries
    so desk cannot be reassigned yet

flush_all_zero_pkmaps():

    clear old directory/TLB information and mark those desks reusable



SECTION 33: SUMMARY
================================================================================

This highmem code implements the classic Linux 32-bit highmem mapping system.

Core responsibilities:

    nr_free_highpages()
        count free highmem allocator pages

    kmap_high()
        get a schedulable kernel virtual address for a highmem page

    kunmap_high()
        drop one active use of that mapping

    flush_all_zero_pkmaps()
        recycle stale pkmap slots after clearing PTEs and flushing TLB

    page_address()
        find current virtual address of a page

    set_page_address()
        record/remove highmem page->virtual association

    page_address_init()
        initialize page->virtual association structures

Key ideas:

    highmem pages lack permanent kernel virtual addresses
    pkmap provides a limited global virtual window for temporary mappings
    pkmap_count is a state machine, not a plain refcount
    count==1 means "unused but not yet TLB-safe to reuse"
    page->virtual associations are tracked in a hash table

In short:

    this code lets the Linux kernel temporarily and safely map highmem pages
    into a small reusable kernel virtual region, while carefully handling
    sharing, sleeping, slot reuse, and TLB correctness.


================================================================================
END OF FILE
================================================================================
