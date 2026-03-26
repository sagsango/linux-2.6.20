================================================================================
FILE: linux_block_bounce_buffers_background_and_flow.txt
TOPIC: Bounce Buffer Handling for Block Devices (bounce.c / block layer)
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHY THIS CODE EXISTS
================================================================================

This file implements BOUNCE BUFFER handling for block I/O.

It exists because not all block devices can DMA to every physical page
in RAM.

Typical hardware limitations:

    1) Old ISA DMA devices
       - can only DMA to low physical memory
       - often below 16 MB

    2) Devices with limited DMA address width
       - 24-bit, 32-bit, etc.
       - cannot reach high physical memory

    3) HIGHMEM pages
       - page exists in RAM
       - but device cannot DMA to it directly


So if a BIO points to pages the device cannot access, the kernel must:

    1) allocate temporary DMA-safe pages
    2) copy data into them for WRITE
    3) submit I/O using those pages
    4) copy data back for READ
    5) free the temporary pages

That temporary page is called a:

    BOUNCE BUFFER



SECTION 2: THE CORE PROBLEM
================================================================================

Imagine this:

    User buffer / page cache page
            |
            v
       BIO references page
            |
            v
       Block device performs DMA

This works only if the device can address that physical page.

But suppose the page is in high memory:

    page_to_pfn(page) = very high PFN

while the device can only DMA up to some limit:

    q->bounce_pfn

Then direct DMA is impossible.

Without bounce buffering:

    device DMA would fail
    data corruption could occur
    hardware may not work correctly


So the block layer inserts an intermediate DMA-safe page.



SECTION 3: BIG PICTURE
================================================================================

Normal I/O path:

    BIO -> original pages -> device DMA


Bounce I/O path:

    BIO -> bounced BIO -> low-memory DMA-safe pages -> device DMA


For WRITE:

    original page
        |
        | copy
        v
    bounce page
        |
        | DMA write by device
        v
    storage


For READ:

    storage
        |
        | DMA read by device
        v
    bounce page
        |
        | copy
        v
    original page



SECTION 4: WHAT A BIO IS IN THIS CONTEXT
================================================================================

A BIO describes block I/O.

It contains an array of segments:

    struct bio_vec

Each bio_vec says:

    which page
    offset within page
    length within page


So a BIO might look like:

    bio
      |
      +-- vec[0] -> page A, offset X, len L
      +-- vec[1] -> page B, offset Y, len M
      +-- vec[2] -> page C, offset Z, len N


Some pages may be device-accessible.
Some may not.

Bounce logic examines each segment.



SECTION 5: GLOBAL POOLS
================================================================================

This file defines two mempools:

    static mempool_t *page_pool, *isa_page_pool;


These pools provide preallocated emergency pages for bounce buffers.

Why mempools?

Because block I/O may happen under memory pressure.

If the system is already low on memory, we still must be able to
allocate bounce pages for essential I/O.

Mempools guarantee forward progress.


Two pools exist:

    page_pool
        general bounce pages (mainly for highmem / normal device limits)

    isa_page_pool
        pages allocated from DMA zone using GFP_DMA
        for very restricted ISA-style devices



SECTION 6: HIGHMEM EMERGENCY POOL INITIALIZATION
================================================================================

Function:

    init_emergency_pool()

Purpose:

    create a small pool of pages for general bounce buffering,
    but only if the system actually has highmem.


Flow:

    si_meminfo(&i)
    si_swapinfo(&i)

    if (!i.totalhigh)
        return 0

    page_pool = mempool_create_page_pool(POOL_SIZE, 0)

Here:

    POOL_SIZE = 64 pages


Meaning:

    if system has highmem, create a pool of 64 pages


Why only on highmem systems?

Because if there is no highmem, many bounce cases disappear,
so the extra pool may not be needed.



SECTION 7: ISA EMERGENCY POOL INITIALIZATION
================================================================================

Function:

    init_emergency_isa_pool()

Purpose:

    create bounce pages specifically in DMA-capable low memory


This uses:

    mempool_alloc_pages_isa()

which internally does:

    mempool_alloc_pages(gfp_mask | GFP_DMA, ...)

So pages come from DMA zone.


Why?

ISA DMA hardware often cannot access normal pages outside the DMA region.

Therefore the bounce page itself must be allocated from low DMA memory.



SECTION 8: HIGHMEM PAGE COPYING
================================================================================

Function:

    bounce_copy_vec(struct bio_vec *to, unsigned char *vfrom)

When CONFIG_HIGHMEM is enabled, a highmem page may not have a permanent
kernel virtual mapping.

So the kernel uses:

    kmap_atomic(page, ...)
    memcpy(...)
    kunmap_atomic(...)

This temporarily maps the page so data can be copied.

Without HIGHMEM:

    page_address(page)

is enough.


So this helper abstracts:

    "copy bytes into the destination bio_vec page safely"



SECTION 9: READ-COPY HELPER
================================================================================

Function:

    copy_to_high_bio_irq(struct bio *to, struct bio *from)

Purpose:

    after a READ completes into bounce pages,
    copy data from bounce BIO back into the original BIO


Flow:

    for each segment in destination/original BIO:
        compare original segment page and bounce segment page
        if same page:
            not bounced, skip
        else:
            copy from bounce page into original page


Important note:

    fromvec->bv_offset and fromvec->bv_len might have been modified
    by lower layers, so the code carefully uses the original shape
    from the destination side.


Meaning:

    original BIO layout is authoritative for final copy-back.



SECTION 10: END-IO OVERVIEW
================================================================================

Once bounced I/O is submitted, the bounced BIO completes first.

But the real user-visible request is the ORIGINAL BIO.

So end-I/O handlers must:

    1) propagate flags/errors
    2) copy back data on READ
    3) free bounce pages
    4) end the original BIO
    5) release temporary bounced BIO



SECTION 11: COMMON END-IO CLEANUP
================================================================================

Function:

    bounce_end_io(struct bio *bio, mempool_t *pool, int err)

Here:

    bio          = bounced BIO
    bio_orig     = original BIO stored in bi_private


It performs:

1) propagate EOPNOTSUPP if set

2) iterate all segments
   for any bounced page:
       dec_zone_page_state(..., NR_BOUNCE)
       mempool_free(bounce_page, pool)

3) complete original BIO:

       bio_endio(bio_orig, bio_orig->bi_size, err)

4) release bounced BIO:

       bio_put(bio)


So this is the common cleanup path for both READ and WRITE.



SECTION 12: WRITE COMPLETION FLOW
================================================================================

Functions:

    bounce_end_io_write()
    bounce_end_io_write_isa()

These are used for WRITE requests.

Why is WRITE simpler?

Because for WRITE, data was copied BEFORE submission:

    original page -> bounce page -> device

So after I/O completes, nothing needs to be copied back.

We only need:

    free bounce pages
    finish original BIO


These handlers wait until:

    bio->bi_size == 0

which means the entire bounced BIO completed.


Then they call:

    bounce_end_io(...)



SECTION 13: READ COMPLETION FLOW
================================================================================

Functions:

    __bounce_end_io_read()
    bounce_end_io_read()
    bounce_end_io_read_isa()

READ is different.

Data path is:

    device -> bounce page -> original page

So on completion:

1) check if BIO_UPTODATE is set
2) if successful, copy data from bounced BIO to original BIO
3) free bounce pages
4) finish original BIO


That happens in:

    __bounce_end_io_read()

with:

    copy_to_high_bio_irq(bio_orig, bio)

followed by cleanup.



SECTION 14: THE CORE BOUNCING FUNCTION
================================================================================

Function:

    __blk_queue_bounce(request_queue_t *q, struct bio **bio_orig, mempool_t *pool)

This is the heart of the file.

Inputs:

    q         = request queue for target block device
    bio_orig  = pointer to original BIO pointer
    pool      = bounce page pool to use


Goal:

    inspect BIO segments
    replace inaccessible pages with bounce pages
    build a new BIO if needed



SECTION 15: STEP-BY-STEP OF __blk_queue_bounce()
================================================================================

STEP 1: Inspect each segment

    bio_for_each_segment(from, *bio_orig, i) {
        page = from->bv_page;

        if (page_to_pfn(page) < q->bounce_pfn)
            continue;
    }

Interpretation:

    if the page PFN is BELOW device limit,
    device can access it directly,
    so no bounce needed.

But if:

    page_to_pfn(page) >= q->bounce_pfn

then the page is too high for the device and must be bounced.



STEP 2: Lazily allocate a new BIO

If the first unaddressable page is found:

    bio = bio_alloc(GFP_NOIO, (*bio_orig)->bi_vcnt);

Why lazy?

Because if no pages need bouncing, there is no reason to allocate
a new BIO at all.



STEP 3: Allocate bounce page for that segment

    to->bv_page = mempool_alloc(pool, q->bounce_gfp);

Also copy length and offset:

    to->bv_len = from->bv_len;
    to->bv_offset = from->bv_offset;

And account it:

    inc_zone_page_state(to->bv_page, NR_BOUNCE);



STEP 4: For WRITE, pre-copy data into bounce page

If request direction is WRITE:

    flush_dcache_page(from->bv_page);
    vto   = page_address(to->bv_page) + to->bv_offset;
    vfrom = kmap(from->bv_page) + from->bv_offset;
    memcpy(vto, vfrom, to->bv_len);
    kunmap(from->bv_page);

Meaning:

    device must write original data to disk,
    so bounce page must contain original data before submission.



STEP 5: If no pages required bounce, return

    if (!bio)
        return;

This is the fast path:
no extra work, original BIO remains unchanged.



STEP 6: Fill non-bounced segments

If at least one page required bounce, a new BIO exists.

Now all remaining segments that did NOT need bounce are copied as-is:

    to->bv_page   = from->bv_page;
    to->bv_len    = from->bv_len;
    to->bv_offset = from->bv_offset;

So the new BIO becomes a full mirror of the old BIO,
except some pages are substituted with DMA-safe bounce pages.



STEP 7: Copy BIO metadata

The bounced BIO inherits:

    bi_bdev
    bi_sector
    bi_rw
    bi_vcnt
    bi_idx
    bi_size

and is marked:

    BIO_BOUNCED



STEP 8: Install proper end_io handler

Depending on pool and READ/WRITE direction:

    general pool + WRITE -> bounce_end_io_write
    general pool + READ  -> bounce_end_io_read
    ISA pool + WRITE     -> bounce_end_io_write_isa
    ISA pool + READ      -> bounce_end_io_read_isa



STEP 9: Save original BIO pointer

    bio->bi_private = *bio_orig;

This is crucial.

The bounced BIO needs to know which original BIO to complete later.



STEP 10: Replace caller's BIO pointer

    *bio_orig = bio;

After this, lower block layer submits the bounced BIO,
not the original one.



SECTION 16: TOP-LEVEL ENTRY POINT
================================================================================

Function:

    blk_queue_bounce(request_queue_t *q, struct bio **bio_orig)

This is the public entry point used by the block layer.


Flow:

1) choose correct pool

    if queue does NOT require GFP_DMA:
        if q->bounce_pfn >= blk_max_pfn
            return
        pool = page_pool

    else:
        pool = isa_page_pool


Important optimization:

    if bounce_pfn is above or equal to highest system PFN,
    every page is already reachable by the device,
    so there is nothing to do.


2) call slow path

    __blk_queue_bounce(q, bio_orig, pool)



SECTION 17: WHY q->bounce_pfn MATTERS
================================================================================

This value represents the highest page frame number
the device can DMA to.

So device-accessible memory range is roughly:

    page_to_pfn(page) < q->bounce_pfn


If page PFN is above that threshold:

    must bounce


Examples:

    q->bounce_pfn = pages below 4GB
    q->bounce_pfn = pages below 16MB
    q->bounce_pfn = full system memory -> no bounce needed



SECTION 18: WHY MEMPOOL IS CRITICAL
================================================================================

Consider swap-over-low-memory or filesystem writeback under pressure.

If bounce pages were allocated with ordinary allocation only:

    memory pressure -> allocation failure
    allocation failure -> cannot submit I/O
    cannot submit I/O -> cannot free memory
    system could deadlock

Mempools solve this by reserving emergency resources.

That guarantees bounce buffering can continue even when memory is tight.



SECTION 19: WRITE FLOW DIAGRAM
================================================================================

WRITE request with inaccessible page:

    Original BIO
        |
        +-- vec[i] points to high/unreachable page
        |
        v
    blk_queue_bounce()
        |
        v
    allocate bounced BIO
        |
        v
    allocate bounce page from pool
        |
        v
    copy original page data -> bounce page
        |
        v
    submit bounced BIO to device
        |
        v
    device DMA reads from bounce page
        |
        v
    I/O completion
        |
        v
    bounce_end_io_write()
        |
        v
    free bounce pages
        |
        v
    complete original BIO



SECTION 20: READ FLOW DIAGRAM
================================================================================

READ request with inaccessible page:

    Original BIO
        |
        +-- vec[i] points to high/unreachable page
        |
        v
    blk_queue_bounce()
        |
        v
    allocate bounced BIO
        |
        v
    allocate bounce page from pool
        |
        v
    submit bounced BIO to device
        |
        v
    device DMA writes data into bounce page
        |
        v
    I/O completion
        |
        v
    bounce_end_io_read()
        |
        v
    copy bounce page -> original page
        |
        v
    free bounce pages
        |
        v
    complete original BIO



SECTION 21: WHY SOME SEGMENTS ARE LEFT UNCHANGED
================================================================================

A BIO can contain many segments.

Only segments above the device DMA limit are bounced.

So final bounced BIO may contain a mix:

    segment 0 -> original page
    segment 1 -> bounce page
    segment 2 -> original page
    segment 3 -> bounce page

This avoids unnecessary copies and unnecessary pool usage.



SECTION 22: WHY CACHE FLUSHES AND KMAP ARE USED
================================================================================

Two important helpers appear:

    flush_dcache_page(...)
    kmap(...) / kmap_atomic(...)

Reason:

1) Highmem pages may not be permanently kernel-mapped
   so temporary mapping is needed for copying.

2) On some architectures cache maintenance is needed so
   CPU-visible data and DMA-visible data remain coherent.

This is especially important around page copying for I/O.



SECTION 23: WHAT THIS CODE ACHIEVES
================================================================================

This code allows the block layer to safely support devices that cannot
DMA to arbitrary physical memory.

It achieves that by:

    examining each BIO segment
    identifying device-inaccessible pages
    allocating safe low-memory replacement pages
    copying data for WRITE
    copying data back for READ
    using mempools for reliability
    preserving original BIO completion semantics


In short:

    bounce buffering hides hardware DMA limitations from upper layers.



SECTION 24: LIMITATIONS / COST
================================================================================

Bounce buffering is correct but expensive.

Costs include:

    extra memory allocation
    extra memcpy
    extra BIO object
    higher CPU overhead
    additional latency

So it is only used when necessary.

If hardware and memory layout allow direct DMA, the fast path avoids it.



SECTION 25: SIMPLE MENTAL MODEL
================================================================================

Think of the device as a truck that can only enter certain roads.

Original page:

    house on a narrow road the truck cannot reach

Bounce page:

    temporary warehouse on an accessible road

WRITE:

    move goods from house to warehouse
    truck picks them up from warehouse

READ:

    truck drops goods at warehouse
    move goods from warehouse to house


That warehouse is the bounce buffer.



SECTION 26: SUMMARY
================================================================================

This source file implements block-layer bounce buffers.

Main purpose:

    make block I/O work when device DMA addressability is limited


Main mechanism:

    inspect BIO pages against queue DMA limit
    replace inaccessible pages with temporary DMA-safe pages
    copy data into bounce pages for WRITE
    copy data back for READ
    free temporary resources on completion


Key functions:

    init_emergency_pool()
        create general bounce-page mempool

    init_emergency_isa_pool()
        create ISA/DMA-zone bounce-page mempool

    __blk_queue_bounce()
        core logic to build bounced BIO

    blk_queue_bounce()
        top-level entry point and fast-path skip logic

    bounce_end_io_*()
        cleanup and READ copy-back handlers


This is how Linux bridges the gap between:

    upper-layer memory pages
and
    lower-layer device DMA limitations


================================================================================
END OF FILE
================================================================================
