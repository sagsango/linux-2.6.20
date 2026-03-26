================================================================================
FILE: linux_memory_hotplug_background_and_flow.txt
TOPIC: Memory Hotplug, Sparse Section Addition, Online Pages, and New Node Bringup
SOURCE: mm/memory_hotplug.c
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHY memory_hotplug.c EXISTS
================================================================================

This file exists to support adding physical memory to a running Linux system.

That means the kernel can do something like:

    "new RAM appeared"
or
    "firmware/platform says this physical memory range is now available"
or
    "a NUMA node with RAM is being hot-added"

and then Linux must integrate that memory into its MM subsystem.

This is not just one action.

To really hot-add memory, the kernel must do several things:

    1) register the new physical range in iomem resource tree
    2) create sparsemem sections / memmap metadata
    3) initialize zones for that memory
    4) mark pages online so the allocator can use them
    5) possibly create a new pgdat for a new NUMA node
    6) start node-level kernel threads like kswapd
    7) rebuild zonelists and global accounting

So this file is the glue between:

    platform/arch memory-add event
and
    Linux page allocator / zones / nodes / sysfs / iomem integration



SECTION 2: BIG PICTURE
================================================================================

This file mainly covers these layers:

A) Physical resource registration

    register_memory_resource()
    release_memory_resource()

B) Sparse memory section and zone growth

    __add_zone()
    __add_section()
    __add_pages()

C) Runtime online transition for pages

    online_pages()

D) New NUMA node pgdat creation

    hotadd_new_pgdat()
    rollback_node_hotadd()

E) Top-level add-memory flow

    add_memory()

So the overall hot-add pipeline is roughly:

    reserve physical range in iomem
        ->
    create sections + memmap + zone metadata
        ->
    optionally create new node data
        ->
    arch-specific add-memory hookup
        ->
    online pages
        ->
    allocator and node become usable



SECTION 3: IMPORTANT CONCEPTS
================================================================================

Before reading the functions, keep these concepts clear:

Memory resource tree

    Linux tracks physical address ownership in iomem_resource.
    New RAM must be registered there as "System RAM".


Sparsemem section

    Physical memory is divided into sections.
    Memory hotplug usually operates at section granularity.


Zone

    Pages belong to zones like DMA, NORMAL, HIGHMEM.
    New memory must be attached to some zone.


pgdat / pglist_data

    Per-NUMA-node memory descriptor.
    If adding memory for a previously offline node, a new pgdat may be needed.


Online page

    A page may exist in metadata but still be reserved/offline.
    Only after online_page() does the allocator really use it.


Zonelists

    Page allocator fallback lists.
    These may need rebuilding if a previously empty zone becomes populated.



SECTION 4: register_memory_resource()
================================================================================

Function:

    static struct resource *register_memory_resource(u64 start, u64 size)

Purpose:

    add the hot-added physical memory range into the global iomem resource tree
    as "System RAM"

Flow:

    res = kzalloc(sizeof(struct resource), GFP_KERNEL)
    BUG_ON(!res)

    res->name  = "System RAM"
    res->start = start
    res->end   = start + size - 1
    res->flags = IORESOURCE_MEM

    if (request_resource(&iomem_resource, res) < 0) {
        printk(...)
        kfree(res)
        res = NULL
    }

    return res

Meaning:

    before Linux MM starts using the range,
    the range must be reserved in the physical resource namespace

Why?

Because iomem_resource prevents overlapping ownership such as:

    firmware regions
    PCI MMIO windows
    already-registered RAM
    ACPI-reserved ranges
    device resources

So this is one of the earliest sanity/ownership checks in hot-add.



SECTION 5: release_memory_resource()
================================================================================

Function:

    static void release_memory_resource(struct resource *res)

Purpose:

    undo the iomem registration if hot-add later fails

Flow:

    if (!res)
        return
    release_resource(res)
    kfree(res)

This is rollback support.

If memory hot-add fails after resource registration, the kernel must clean up
and return the physical range to the global resource tree.



SECTION 6: HOTPLUG WITH SPARSEMEM
================================================================================

Most of the interesting section-add logic is inside:

#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE

This means the kernel is using sparse memory model for hotplug.

Why sparsemem matters:

    with memory hotplug, physical memory may appear/disappear in discontinuous
    chunks, so the kernel cannot assume one monolithic contiguous mem_map

Sparsemem gives Linux a section-based model:

    memory divided into sections
    each section can be added and initialized independently

That is why functions here operate with:

    PAGES_PER_SECTION
    pfn_to_section_nr()
    __pfn_to_section()



SECTION 7: __add_zone()
================================================================================

Function:

    static int __add_zone(struct zone *zone, unsigned long phys_start_pfn)

Purpose:

    ensure the target zone is initialized and initialize memmap metadata
    for the new section's pages

Flow:

    pgdat = zone->zone_pgdat
    nr_pages = PAGES_PER_SECTION
    nid = pgdat->node_id
    zone_type = zone - pgdat->node_zones

    if (!populated_zone(zone)) {
        ret = init_currently_empty_zone(zone, phys_start_pfn,
                                        nr_pages, MEMMAP_HOTPLUG);
        if (ret < 0)
            return ret;
    }

    memmap_init_zone(nr_pages, nid, zone_type,
                     phys_start_pfn, MEMMAP_HOTPLUG);
    return 0

Meaning:

    if this zone has never contained pages before,
    initialize it as an empty zone becoming active

Then:

    initialize struct page metadata for the new page range

So this function is about:

    zone metadata + memmap setup for one section



SECTION 8: WHY __add_zone() HAS TWO PHASES
================================================================================

There are really two different problems:

1) Zone object itself may be empty/uninitialized for practical use

    handled by:
        init_currently_empty_zone(...)

2) struct page array / memmap entries for the incoming PFN range must be set up

    handled by:
        memmap_init_zone(...)

So:

    empty zone initialization
    and
    page metadata initialization

are separate steps.



SECTION 9: __add_section()
================================================================================

Function:

    static int __add_section(struct zone *zone, unsigned long phys_start_pfn)

Purpose:

    add one sparsemem section to the system and integrate it into MM

Flow:

    nr_pages = PAGES_PER_SECTION

    if (pfn_valid(phys_start_pfn))
        return -EEXIST

So if PFN already valid, section already exists.

Then:

    ret = sparse_add_one_section(zone, phys_start_pfn, nr_pages)
    if (ret < 0)
        return ret

This creates sparsemem section metadata.

Then:

    ret = __add_zone(zone, phys_start_pfn)
    if (ret < 0)
        return ret

This initializes zone/memmap state for that section.

Then:

    return register_new_memory(__pfn_to_section(phys_start_pfn))

This exports/registers the new memory section to higher-level hotplug memory
subsystem (for memory blocks/sysfs integration).


So one section-add path is:

    sparse section metadata
        ->
    zone + memmap init
        ->
    register as new memory block/section



SECTION 10: __add_pages()
================================================================================

Function:

    int __add_pages(struct zone *zone,
                    unsigned long phys_start_pfn,
                    unsigned long nr_pages)

Purpose:

    generic helper to add a physical PFN range into a chosen zone

Comment says:

    arch code that supports memory hotplug is expected to call this
    after deciding which zone should own the new memory

Flow:

    start_sec = pfn_to_section_nr(phys_start_pfn)
    end_sec   = pfn_to_section_nr(phys_start_pfn + nr_pages - 1)

    for each section i in [start_sec, end_sec]:
        err = __add_section(zone, i << PFN_SECTION_SHIFT)

        if (err && err != -EEXIST)
            break
        err = 0

Meaning:

    align the range to sparse sections
    add each section one by one

Special note from comment:

    -EEXIST is tolerated here
    later collision/resource logic handles the final visibility issues

So __add_pages() is the generic "materialize this PFN range in sparsemem + zone"
helper.



SECTION 11: grow_zone_span()
================================================================================

Function:

    static void grow_zone_span(struct zone *zone,
                               unsigned long start_pfn,
                               unsigned long end_pfn)

Purpose:

    expand the zone's spanned PFN range so it covers the new memory

Flow:

    zone_span_writelock(zone)

    old_zone_end_pfn = zone->zone_start_pfn + zone->spanned_pages

    if (start_pfn < zone->zone_start_pfn)
        zone->zone_start_pfn = start_pfn

    zone->spanned_pages = max(old_zone_end_pfn, end_pfn)
                          - zone->zone_start_pfn

    zone_span_writeunlock(zone)

Meaning:

    zone's physical span now includes the added pages

Important distinction:

    spanned_pages
        counts total PFN span covered by zone, including holes

not just:

    present_pages

This matters because hot-added memory may enlarge the zone coverage window.



SECTION 12: grow_pgdat_span()
================================================================================

Function:

    static void grow_pgdat_span(struct pglist_data *pgdat,
                                unsigned long start_pfn,
                                unsigned long end_pfn)

Purpose:

    expand the NUMA node's spanned PFN range

Flow:

    old_pgdat_end_pfn = pgdat->node_start_pfn + pgdat->node_spanned_pages

    if (start_pfn < pgdat->node_start_pfn)
        pgdat->node_start_pfn = start_pfn

    pgdat->node_spanned_pages =
        max(old_pgdat_end_pfn, end_pfn) - pgdat->node_start_pfn

This is the node-level analogue of grow_zone_span().

So after hot-add:

    zone span updated
    node span updated

before pages are actually onlined.



SECTION 13: online_pages() BACKGROUND
================================================================================

Function:

    int online_pages(unsigned long pfn, unsigned long nr_pages)

Purpose:

    convert a newly-added PFN range from reserved/offline metadata state into
    actually online allocator-usable pages

This is one of the most important transitions in memory hotplug.

Before online_pages():

    struct page may exist
    section may exist
    zone metadata may exist
    but allocator may still not use the pages

After online_pages():

    pages become available to buddy/page allocator
    present_pages counters increase
    zonelists/global accounting may be updated



SECTION 14: online_pages() STEP-BY-STEP
================================================================================

STEP 1: Determine target zone

    zone = page_zone(pfn_to_page(pfn))

Because section and memmap were already created, pfn_to_page() works.


STEP 2: Resize node/zone spans under pgdat resize lock

    pgdat_resize_lock(...)
    grow_zone_span(zone, pfn, pfn + nr_pages)
    grow_pgdat_span(zone->zone_pgdat, pfn, pfn + nr_pages)
    pgdat_resize_unlock(...)

So the node and zone PFN coverage are enlarged.


STEP 3: Determine whether zonelists must be rebuilt

    if (!populated_zone(zone))
        need_zonelists_rebuild = 1

Why?

If this zone was previously unpopulated, the page allocator may not have been
considering it in zonelists at all.


STEP 4: Build resource window for scanning actual system RAM

    res.start = pfn << PAGE_SHIFT
    res.end   = res.start + (nr_pages << PAGE_SHIFT) - 1
    res.flags = IORESOURCE_MEM

    section_end = res.end

Then loop:

    while ((res.start < res.end) && (find_next_system_ram(&res) >= 0)) { ... }

This walks real RAM subregions inside the requested range.


STEP 5: For each discovered system RAM chunk, online reserved pages

    start_pfn = res.start >> PAGE_SHIFT
    nr_pages  = (res.end + 1 - res.start) >> PAGE_SHIFT

    if (PageReserved(pfn_to_page(start_pfn))) {
        for each page in chunk:
            online_page(page)
            onlined_pages++
    }

Meaning:

    pages in this RAM chunk are transitioned from reserved/offline into online


STEP 6: Advance resource scan window

    res.start = res.end + 1
    res.end   = section_end


STEP 7: Update present page counters

    zone->present_pages += onlined_pages
    zone->zone_pgdat->node_present_pages += onlined_pages


STEP 8: Recompute watermarks and zonelists/global totals

    setup_per_zone_pages_min()

    if (need_zonelists_rebuild)
        build_all_zonelists()

    vm_total_pages = nr_free_pagecache_pages()
    writeback_set_ratelimit()

Meaning:

    allocator thresholds
    zonelists
    global memory totals
    writeback throttling limits

must all be refreshed after new pages appear.



SECTION 15: WHY online_pages() USES find_next_system_ram()
================================================================================

Even though the caller specifies a PFN range, Linux still re-scans actual
System RAM subranges via resource tree.

Why?

Because the hot-added interval may not be one clean uninterrupted RAM chunk in
resource terms, or some parts may not actually be valid System RAM.

So online_pages() is careful to only online:

    subranges that resource tree identifies as System RAM

This protects correctness against holes or mismatched ranges.



SECTION 16: hotadd_new_pgdat()
================================================================================

Function:

    static pg_data_t *hotadd_new_pgdat(int nid, u64 start)

Purpose:

    create and initialize a new pgdat (NUMA node memory descriptor)
    when adding memory to a node that is not currently online

Flow:

    start_pfn = start >> PAGE_SHIFT

    pgdat = arch_alloc_nodedata(nid)
    if (!pgdat)
        return NULL

    arch_refresh_nodedata(nid, pgdat)

Then initialize node with empty zones:

    unsigned long zones_size[MAX_NR_ZONES] = {0}
    unsigned long zholes_size[MAX_NR_ZONES] = {0}

    free_area_init_node(nid, pgdat, zones_size, start_pfn, zholes_size)

Comment explains:

    init node's zones as empty zones, we don't have any present pages

Meaning:

    node metadata exists now
    zones exist, but with no present pages yet
    actual pages will be added/onlined later

So this is the "new NUMA node skeleton" constructor.



SECTION 17: rollback_node_hotadd()
================================================================================

Function:

    static void rollback_node_hotadd(int nid, pg_data_t *pgdat)

Purpose:

    undo pgdat creation if later steps fail before node is committed online

Flow:

    arch_refresh_nodedata(nid, NULL)
    arch_free_nodedata(pgdat)

So this is rollback support for:

    newly created node data structures

before point-of-no-return.



SECTION 18: add_memory() BACKGROUND
================================================================================

Function:

    int add_memory(int nid, u64 start, u64 size)

Purpose:

    top-level API to add a physical memory range to a NUMA node

This is the central orchestration function in the file.

It handles:

    iomem registration
    optional new pgdat/node creation
    kswapd startup
    arch-specific hot-add
    node online transition
    cpuset tracking
    sysfs node registration

So this is the main entry point for memory hotplug add operation.



SECTION 19: add_memory() STEP-BY-STEP
================================================================================

STEP 1: Register physical range in iomem tree

    res = register_memory_resource(start, size)
    if (!res)
        return -EEXIST

If resource already overlaps/collides, hot-add fails early.


STEP 2: If node is not online, create new pgdat

    if (!node_online(nid)) {
        pgdat = hotadd_new_pgdat(nid, start)
        if (!pgdat)
            return -ENOMEM

        new_pgdat = 1

        ret = kswapd_run(nid)
        if (ret)
            goto error
    }

Meaning:

    if this memory belongs to a brand new node,
    create node metadata and start kswapd for that node


STEP 3: Call arch-specific hot-add logic

    ret = arch_add_memory(nid, start, size)
    if (ret < 0)
        goto error

This is where architecture-specific mechanisms create/attach actual memory
sections and memmap backing. Typically arch code will call helpers like
__add_pages() after deciding target zone(s).


STEP 4: Point of no return - mark node online

    node_set_online(nid)

Comment:

    we online node here. we can't roll back from here.

This is crucial:
    after this point, rollback is no longer supported in this function.


STEP 5: Update cpuset node tracking

    cpuset_track_online_nodes()

So cpusets see the newly online node.


STEP 6: If this was a new pgdat/node, register it in sysfs

    if (new_pgdat) {
        ret = register_one_node(nid)
        BUG_ON(ret)
    }

Comment explains:
    if sysfs node registration fails, there is effectively no rollback path
    so kernel BUG_ONs rather than trying to limp onward


STEP 7: Return success


Error path before node online:

    if (new_pgdat)
        rollback_node_hotadd(nid, pgdat)
    if (res)
        release_memory_resource(res)

So rollback covers:
    pgdat creation
    iomem resource registration

but not after node_set_online()



SECTION 20: WHY kswapd_run() HAPPENS FOR NEW NODE
================================================================================

A new NUMA node needs its own memory management support threads, especially:

    kswapd

kswapd is the background reclaim daemon for a node.

If a new node is created without a running kswapd:

    reclaim behavior for that node would be incomplete/broken

So for brand-new nodes:

    create pgdat
    start kswapd
    then continue with memory add



SECTION 21: WHY node_set_online() IS A POINT OF NO RETURN
================================================================================

Once the node becomes globally visible as online:

    cpusets may observe it
    sysfs registration may happen
    allocator/reclaim policies may incorporate it
    other subsystems may begin using it

At that point, backing out cleanly would require much broader coordinated
rollback across multiple subsystems.

So add_memory() deliberately treats:

    node_set_online(nid)

as the commit point.



SECTION 22: COMPLETE HOT-ADD FLOW
================================================================================

Top-level hot-add request:
    add_memory(nid, start, size)
        |
        +--> register_memory_resource(start, size)
        |       reserve physical range in iomem tree as System RAM
        |
        +--> if node nid not online:
        |       hotadd_new_pgdat(nid, start)
        |           |
        |           +--> arch_alloc_nodedata()
        |           +--> arch_refresh_nodedata()
        |           +--> free_area_init_node(... empty zones ...)
        |       kswapd_run(nid)
        |
        +--> arch_add_memory(nid, start, size)
        |       |
        |       +--> arch-specific path
        |       +--> often ends up using __add_pages(zone, start_pfn, nr_pages)
        |               |
        |               +--> for each sparse section:
        |                       __add_section(zone, section_pfn)
        |                           |
        |                           +--> sparse_add_one_section(...)
        |                           +--> __add_zone(...)
        |                           |       |
        |                           |       +--> init_currently_empty_zone() if needed
        |                           |       +--> memmap_init_zone(...)
        |                           +--> register_new_memory(section)
        |
        +--> node_set_online(nid)
        +--> cpuset_track_online_nodes()
        +--> if brand new node:
        |       register_one_node(nid)
        |
        v
    return success

Later / separately, pages may be onlined:
    online_pages(pfn, nr_pages)
        |
        +--> grow zone/node span
        +--> scan actual System RAM chunks
        +--> online_page(page) for reserved pages
        +--> update present_pages counts
        +--> rebuild zonelists if needed
        +--> refresh watermarks/global totals/writeback rate



SECTION 23: RELATION BETWEEN __add_pages() AND online_pages()
================================================================================

These two are related but not the same.

__add_pages()

    creates metadata / sparse sections / memmap / zone presence
    makes the PFN range known to the MM system

online_pages()

    transitions those pages into allocator-usable online pages

So a useful mental model is:

    __add_pages()  = "teach kernel about this memory"
    online_pages() = "let allocator actually use this memory"

This separation is very common in memory hotplug:
    add memory
    then online memory



SECTION 24: WHY PRESENT_PAGES IS UPDATED IN online_pages(), NOT __add_pages()
================================================================================

Because:

    spanned_pages
        means the zone covers that PFN span

while:

    present_pages
        means actual present usable pages currently online in the zone

When sections are first added, the memory may still be reserved/offline.

Only after online_page() transitions pages do they count as:

    present_pages
    node_present_pages

That is why these counters are updated in online_pages().



SECTION 25: SIMPLE MENTAL MODEL
================================================================================

Think of memory hotplug like opening a new warehouse area in a running city.

Step 1: Register the land parcel officially

    register_memory_resource()

Step 2: Build the warehouse records and shelves in the city database

    __add_section()
    __add_zone()
    __add_pages()

Step 3: If this is a brand new district, create district administration

    hotadd_new_pgdat()
    kswapd_run()

Step 4: Mark the district as active

    node_set_online()

Step 5: Put actual goods on shelves for public use

    online_pages()

Step 6: Update all routing tables and logistics limits

    build_all_zonelists()
    setup_per_zone_pages_min()
    writeback_set_ratelimit()



SECTION 26: SUMMARY
================================================================================

mm/memory_hotplug.c implements the core generic logic for adding memory to a
running Linux system.

Main responsibilities:

    register new RAM range in iomem resource tree
    add sparsemem sections and memmap metadata
    initialize or extend zones
    expand node/zone spans
    online pages so allocator can use them
    create pgdat for new NUMA nodes
    start kswapd for new nodes
    mark node online and update cpuset/sysfs visibility
    rebuild zonelists and refresh memory accounting after online

Key functions:

    register_memory_resource()
        reserve physical range as System RAM

    __add_zone()
        initialize empty zone if needed and init memmap for new section

    __add_section()
        add one sparse section and register it

    __add_pages()
        generic helper to add PFN range section by section

    online_pages()
        convert newly added pages into online allocator-usable pages

    hotadd_new_pgdat()
        create empty pgdat/zones for brand new NUMA node

    add_memory()
        top-level orchestration for memory hot-add

Core idea to remember:

    memory hotplug is not one step.
    The kernel must separately:
        reserve the physical range,
        create metadata,
        attach it to node/zone structures,
        and finally online the pages.

In short:

    this file is the generic MM control path that turns a newly appeared RAM
    range into real Linux memory that nodes, zones, and the page allocator can
    recognize and use.


================================================================================
END OF FILE
================================================================================
