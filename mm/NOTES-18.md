# NOTE:
    node = numa node


# Hot-plug
# Detection by fiormware:Firmware signals a memory hot-add event to the kernel.
ACPI: Device Hotplug → ACPI0004 (Memory Device)
        
The ACPI driver handling memory devices (drivers/acpi/acpi_memhotplug.c) triggers:
                acpi_memory_device_add()  
                    → acpi_memory_enable_device()
                        → acpi_memory_device_add_memory()
                            → add_memory(nid, start, size)

    
ACPI firmware
    │  (Notify: memory device added)
    ▼
ACPI hotplug driver: acpi_memhotplug.c
    │
    ▼
acpi_memory_device_add_memory()
    │
    ▼
arch_add_memory()   ← architecture-specific wrapper
    │
    ▼
add_memory(nid, start, size)   ← Generic kernel entry point


# Detection by mannual-user-plugin: Userspace Hot-Add via sysfs
Userspace can manually add memory using:
    echo online > /sys/devices/system/memory/memoryXXX/state
Or:
    echo add > /sys/devices/system/memory/probe
The sysfs memory subsystem (drivers/base/memory.c) handles this:
memory_probe_store()
  → arch_add_memory()
      → add_memory(nid, start, size)

Userspace:
   echo add > /sys/devices/system/memory/probe
        │
        ▼
    drivers/base/memory.c: memory_probe_store()
        │
        ▼
    arch_add_memory()
        │
        ▼
    add_memory(nid, start, size)



# Detection summary:
        MEMORY HOT-ADD FLOW
──────────────────────────────────────────────
Userspace / Firmware / Driver
        │
        ▼
(Probes new physical RAM region)
        │
        ▼
arch_add_memory(nid, start, size)
        │
        ▼
add_memory(nid, start, size)
        │
        ├── register_memory_resource()
        ├── hotadd_new_pgdat()   (if new NUMA node)
        ├── __add_pages()
        │      └── sparse_add_one_section()
        │              └── memmap_init_zone()
        ├── build_all_zonelists()
        └── vm_total_pages update
──────────────────────────────────────────────
Memory becomes usable by buddy allocator



# Hot-plug main flow

┌────────────────────────────────────────────────────────────────────────────┐
│                            add_memory(nid, start, size)                    │
└────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
              ┌──────────────────────────────────────────┐
              │ register_memory_resource(start, size)    │
              │   - create struct resource               │
              │   - mark range as "System RAM"           │
              │   - insert into iomem_resource tree      │
              └──────────────────────────────────────────┘
                                      │
                   success? ──────────┘
                        │YES
                        ▼
      ┌────────────────────────────────────────────────────────────┐
      │ If node is offline → create pgdat for new NUMA node        │
      │    pgdat = hotadd_new_pgdat(nid, start)                    │
      │    free_area_init_node() initializes empty zones           │
      │    kswapd_run(nid)                                         │
      └────────────────────────────────────────────────────────────┘
                        │
                        ▼
     ┌──────────────────────────────────────────────────────────┐
     │ Call architecture-specific memory add                    │
     │ arch_add_memory(nid, start, size)                        │
     │ Typically calls:                                         │
     │   → __add_pages()                                        │
     │   → sparse_add_one_section()                             │
     │   → memmap_init_zone()                                   │
     └──────────────────────────────────────────────────────────┘
                        │
                        ▼
           ┌──────────────────────────────────────────┐
           │ If success → mark NUMA node online       │
           │ node_set_online(nid)                     │
           │ cpuset_track_online_nodes()              │
           │ register_one_node(nid) (sysfs init)      │
           └──────────────────────────────────────────┘
                        │
                        ▼
         ┌────────────────────────────────────────────┐
         │ MEMORY IS NOW “PHYSICALLY ADDED” but NOT   │
         │ “ONLINE”. That is done by:                 │
         │ → online_pages(pfn, nr_pages)              │
         └────────────────────────────────────────────┘


#  __add_pages(): Add sparsemem sections**

 __add_pages(zone, start_pfn, nr_pages)
              │
              ▼
    ┌──────────────────────────────────────────────┐
    │ Convert pfn range into section numbers       │
    │  start_sec = pfn_to_section_nr(start_pfn)    │
    │  end_sec   = pfn_to_section_nr(last_pfn)     │
    └──────────────────────────────────────────────┘
              │
              ▼
    ┌──────────────────────────────────────────────┐
    │ Loop for each section                        │
    │   __add_section(zone, sec_pfn)               │
    └──────────────────────────────────────────────┘
              │
              ▼
            __add_section():
              │
              ▼
   ┌────────────────────────────────────────────────────────┐
   │ 1. Check if pfn_valid() — if already present → EEXIST  │
   └────────────────────────────────────────────────────────┘
              │
              ▼
   ┌────────────────────────────────────────────────────────┐
   │ 2. sparse_add_one_section():                           │
   │    - allocate mem_map for this section                 │
   │    - allocate struct page array                        │
   │    - add to sparsemem section list                     │
   └────────────────────────────────────────────────────────┘
              │
              ▼
   ┌────────────────────────────────────────────────────────┐
   │ 3. __add_zone():                                        │
   │    - if zone empty → init_currently_empty_zone()       │
   │    - memmap_init_zone(): initialize struct page fields │
   └────────────────────────────────────────────────────────┘
              │
              ▼
   ┌────────────────────────────────────────────────────────┐
   │ 4. register_new_memory(section) → userspace sysfs info │
   └────────────────────────────────────────────────────────┘



#  online_pages(): Bring pages into allocator**

 online_pages(pfn, nr_pages)
       │
       ▼
   ┌──────────────────────────────────────────────┐
   │ Determine zone = page_zone(pfn_to_page(pfn)) │
   └──────────────────────────────────────────────┘
       │
       ▼
   ┌──────────────────────────────────────────────┐
   │ pgdat_resize_lock(pgdat)                     │
   │   grow_zone_span(zone, start,end)            │
   │   grow_pgdat_span(pgdat, start,end)          │
   │ pgdat_resize_unlock(pgdat)                   │
   └──────────────────────────────────────────────┘
       │
       ▼
   ┌─────────────────────────────────────────────────────────────┐
   │ Walk through memory resource ranges (find_next_system_ram)  │
   │ For every pfn in this online span:                          │
   │     if PageReserved(page):                                  │
   │         online_page(page)   ← this clears PageReserved      │
   │         onlined_pages++                                      │
   └─────────────────────────────────────────────────────────────┘
       │
       ▼
   ┌─────────────────────────────────────────────────────────────┐
   │ Update accounting:                                           │
   │    zone->present_pages += onlined_pages                      │
   │    pgdat->node_present_pages += onlined_pages                │
   └─────────────────────────────────────────────────────────────┘
       │
       ▼
   ┌─────────────────────────────────────────────────────────────┐
   │ setup_per_zone_pages_min()                                  │
   │ if zone was not populated → rebuild zonelists               │
   │ build_all_zonelists()                                       │
   │ vm_total_pages = nr_free_pagecache_pages()                  │
   │ writeback_set_ratelimit()                                   │
   └─────────────────────────────────────────────────────────────┘


# FINAL SYSTEM VIEW AFTER HOT-ADD
┌─────────────────────────────────────────────────────────────────────────────┐
│                          FINAL SYSTEM VIEW (POST HOT-ADD)                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ 1) PHYSICAL / RESOURCE LAYER                                                │
│                                                                             │
│   iomem_resource                                                            │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │ ...                                                                 │   │
│   │ [System RAM]  start=START  end=START+SIZE-1  (from register_memory_ │   │
│   │                resource())                                           │   │
│   │ ...                                                                 │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│   → New RAM range is now a child “System RAM” resource (non-overlapping).   │
├─────────────────────────────────────────────────────────────────────────────┤
│ 2) NODE / PGDAT TOPOLOGY                                                    │
│                                                                             │
│   NODE n (online)                                                           │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │ pgdat (NODE_DATA(n))                                                │   │
│   │  • node_start_pfn  ──┐                                              │   │
│   │  • node_spanned_pages│  updated by grow_pgdat_span()                │   │
│   │  • node_present_pages┘  += onlined_pages                            │   │
│   │  • kswapd, reclaim_state, per-node stats                            │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│   → If the node was new: pgdat allocated (hotadd_new_pgdat), zones init,    │
│     sysfs node created (register_one_node), kswapd bound (kswapd_run).       │
├─────────────────────────────────────────────────────────────────────────────┤
│ 3) SPARSEMEM / SECTION & MEMMAP                                             │
│                                                                             │
│   For each hot-add section:                                                 │
│     ┌───────────────────────────────────────────────────────────────────┐   │
│     │ struct mem_section (valid)                                        │   │
│     │   • pfn range covers 1 section (= PAGES_PER_SECTION)              │   │
│     │   • memmap (struct page[]) allocated & initialized                │   │
│     └───────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│   → __add_section(): sparse_add_one_section() + __add_zone() + register_    │
│     new_memory(). pfn_valid() now true in that range.                       │
├─────────────────────────────────────────────────────────────────────────────┤
│ 4) ZONES INSIDE THE NODE                                                    │
│                                                                             │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │ ZONE_DMA / ZONE_NORMAL / ZONE_HIGHMEM (as arch defines)             │   │
│   │   • zone_start_pfn, spanned_pages   (grow_zone_span)                │   │
│   │   • present_pages                 += onlined_pages                   │   │
│   │   • watermarks (min/low/high) recalculated                           │   │
│   │   • LRU lists, free_area[] contain new pageblocks after onlining     │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│   → online_pages():                                                         │
│       - walks System RAM subranges (find_next_system_ram)                   │
│       - for each pfn: online_page(page) → unreserve, free to buddy          │
│       - updates zone->present_pages and pgdat->node_present_pages           │
├─────────────────────────────────────────────────────────────────────────────┤
│ 5) BUDDY ALLOCATOR / PAGE OWNER STATE                                       │
│                                                                             │
│   Buddy per-zone free_area[]:                                               │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │  order 0 … MAX_ORDER-1                                              │   │
│   │   • New pageblocks from hot-add inserted (as Free)                   │   │
│   │   • Coalesced up to the largest possible order                       │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│   → New pages are now part of the global free pool used by allocators.      │
├─────────────────────────────────────────────────────────────────────────────┤
│ 6) VM ACCOUNTING & GLOBAL STATE                                             │
│                                                                             │
│   • setup_per_zone_pages_min(): recompute per-zone min/free targets         │
│   • build_all_zonelists() if zone was previously unpopulated                │
│       → per-node zonelists include the newly usable zone(s)                 │
│   • vm_total_pages = nr_free_pagecache_pages() (recomputed)                 │
│   • writeback_set_ratelimit(): I/O pacing rebalanced                        │
│   • cpuset_track_online_nodes(): cpusets see the node as online             │
├─────────────────────────────────────────────────────────────────────────────┤
│ 7) FAST PATH IMPACT (ALLOCATORS & RECLAIM)                                   │
│                                                                             │
│   __alloc_pages(gfp, order)                                                 │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │  consult zonelists  → includes new zone(s) if rebuilt               │   │
│   │  try direct reclaim if needed: try_to_free_pages()                   │   │
│   │  may wake kswapd via wakeup_kswapd()                                 │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│   kswapd (per node)                                                         │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │  scans LRU, shrinks slab, balances watermarks using new capacity    │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────────────┤
│ 8) CONTROL PLANE / SYSFS / POLICY                                           │
│                                                                             │
│   • memory block devices → online state reflects addition                   │
│   • /sys/devices/system/node/nodeN/ present/online                         │
│   • cpusets / NUMA policies can target new node/zone                        │
└─────────────────────────────────────────────────────────────────────────────┘


                ╔════════════════════════════════════════╗
                ║      SYSTEM AFTER MEMORY HOTPLUG       ║
                ╠════════════════════════════════════════╣
                ║ 1. New memory section exists            ║
                ║ 2. Its struct page[] array allocated    ║
                ║ 3. Zone spanned_pages expanded          ║
                ║ 4. pgdat node_spanned_pages expanded    ║
                ║ 5. Pages marked online + free           ║
                ║ 6. Page allocator can now allocate from ║
                ║    this memory                          ║
                ║ 7. kswapd running for that node         ║
                ║ 8. sysfs entries created for the node   ║
                ║ 9. numa policy + cpusets updated        ║
                ╚════════════════════════════════════════╝



# Hot-remove
Setp #1. Userspace triggers removal
    /sys/devices/system/memory/memoryXXX/state = "offline"

Setp #2: Kernel begins page migration (critical step)
Before migration:
-------------------------------------------------------
PFN range to be removed:
[ unmovable | filecache | anon | kernel | slab | free ]
-------------------------------------------------------

During migration:
-------------------------------------------
[ migrate → other zones OR writeback → disk ]
-------------------------------------------

After migration:
---------------------
[ free free free free ]  ✅ safe to remove
---------------------

Step #3: offline_pages()
Once all pages in the section are isolated + movable + free
    offline_pages(start_pfn, nr_pages)
        mark every page as PageReserved
        remove them from buddy allocator
        zone->present_pages -= nr_pages
        node_present_pages -= nr_pages
        free_area[] no longer contains these pages
Step #4: Sparsemem Section Removal
    sparse_remove_one_section()
        struct mem_section
        struct page *memmap   (vmemmap)
        pfn → page mapping disappears
Setp 5: Release IOMEM resource
    release_memory_resource(res)
        iomem_resource
        └── System RAM 0000A0000000 - 0000A3FFFFFF   (REMOVED)
Step 6: Shrink spans & possibly offline node
    Zone shrink:
        grow_zone_span() reversed
        zone->spanned_pages -= removed_pages
        zone->present_pages -= removed_pages
    Node shrink:
        pgdat->node_spanned_pages -= removed
        pgdat->node_present_pages -= removed

# FINAL SYSTEM VIEW AFTER HOT-REMOVE
┌──────────────────────────────────────────────────────────────────────────────┐
│                           FINAL SYSTEM VIEW (POST HOT-REMOVE)                │
├──────────────────────────────────────────────────────────────────────────────┤
│ 1) RESOURCE LAYER                                                            │
│                                                                              │
│   iomem_resource                                                             │
│   ┌───────────────────────────────────────────────────────────────────────┐  │
│   │ System RAM range removed (release_memory_resource)                    │  │
│   │ No conflicting RAM entry at this PFN range                            │  │
│   └───────────────────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────────────────┤
│ 2) SPARSEMEM                                                                 │
│                                                                              │
│   mem_section[removed]                                                       │
│   ┌───────────────────────────────────────────────────────────────────────┐  │
│   │ valid = 0                                                              │  │
│   │ memmap removed (struct page[] freed)                                   │  │
│   │ pfn_valid() → false for removed PFNs                                   │  │
│   └───────────────────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────────────────┤
│ 3) ZONE VIEW                                                                  │
│                                                                              │
│   zone->present_pages -= removed_pages                                       │
│   zone->spanned_pages shrinks                                                │
│                                                                              │
│   Free area after removal:                                                   │
│      No free areas in removed PFN range                                      │
│      Buddy allocator no longer owns those PFNs                               │
├──────────────────────────────────────────────────────────────────────────────┤
│ 4) NODE VIEW                                                                  │
│                                                                              │
│   pgdat->node_present_pages -= removed_pages                                  │
│   If this hits zero → node offline                                           │
│                                                                              │
│   kswapd for this node:                                                      │
│     - still running if any zones remain                                      │
│     - stopped if node becomes empty                                          │
├──────────────────────────────────────────────────────────────────────────────┤
│ 5) VM SYSTEM STATE                                                            │
│                                                                              │
│   • build_all_zonelists() reruns (if topology changed)                        │
│   • setup_per_zone_pages_min() recalculated                                   │
│   • vm_total_pages updated                                                     │
│   • cpuset updates node masks                                                  │
│                                                                              │
│   Fast-path allocators (__alloc_pages) will no longer consider removed zone   │
│   or removed PFN ranges                                                       │
└──────────────────────────────────────────────────────────────────────────────┘



