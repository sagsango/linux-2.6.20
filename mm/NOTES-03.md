# Hierarchical View (Summary Tree)
User-space
└── Syscalls
    ├── mmap.c / do_brk / VMA management
    │    └── page fault → alloc_pages()
    │         └── page_alloc.c
    │              ├── zonelist / mmzone.c
    │              ├── per-CPU pagesets
    │              └── buddy allocator
    │                   └── struct page[]
    │
    ├── kmalloc() / slab.c
    │    └── alloc_pages()
    │
    ├── vmalloc() / vmalloc.c
    │    └── alloc_pages() + map kernel virtual
    │
    ├── filemap.c / page cache
    │    ├── add_to_page_cache()
    │    └── LRU (vmscan.c)
    │         └── reclaim → swap.c / free_pages()
    │
    ├── swap.c / page_io.c
    │    └── I/O to swap devices
    │
    └── OOM / kswapd
         ├── vmscan.c
         └── oom_kill.c

# Core Relationships (data structure perspective)
pg_data_t (per node)
 ├── node_zones[] ──────→ struct zone
 │                        ├── free_area[MAX_ORDER] → buddy lists
 │                        ├── per-cpu pagesets
 │                        ├── lru (active/inactive)
 │                        └── watermarks
 │
 └── node_mem_map[] ────→ struct page (1 per PFN)



# Typical Allocation Flow Examples
# Example #1: kmalloc(128)
kmalloc(128)
 → find kmalloc-128 cache
 → slab.c: kmem_cache_alloc()
     → if slab empty → alloc_pages(order=0)
         → __alloc_pages() → buddy allocator
 → return object


# Example #2: mmap() + page fault
do_mmap()
 → create vm_area_struct
 → page fault → do_page_fault()
     → handle_mm_fault()
         → alloc_pages()
             → buddy allocator
         → map page into PTE
 
# Example #3: File read (page cache)
generic_file_read()
 → filemap_nopage()
     → find_get_page()
         → miss → alloc_pages()
             → fill from disk
     → add_to_page_cache()

# Example #4: Reclaim + kswapd
alloc_pages()
 → zone_watermark_ok() fails
 → wakeup_kswapd()
     → scan inactive list
         → try_to_unmap()
         → free_pages()


# MEMORY MANAGEMENT — FLOW DIAGRAM

                         ┌────────────────────────────────────────────┐
                         │          User-space Process (malloc, mmap) │
                         └────────────────────────────────────────────┘
                                              │
                 ┌────────────────────────────┴────────────────────────────┐
                 │                         System Calls                    │
                 │              (brk, mmap, mremap, mprotect)              │
                 └────────────────────────────┬────────────────────────────┘
                                              │
                                              ▼
                             ┌────────────────────────────────┐
                             │   do_mmap(), do_brk(), etc.    │  ← mm/mmap.c
                             │   → create vm_area_structs      │
                             │   → setup page tables lazily    │
                             └────────────────────────────────┘
                                              │
                                              ▼
                                Page Fault occurs (no page)
                                              │
                                              ▼
                          ┌────────────────────────────────────────┐
                          │       do_page_fault() (arch code)      │
                          │ → handle_mm_fault() → fault handler     │
                          │    (anonymous/file-backed page)         │
                          └────────────────────────────────────────┘
                                              │
                                              ▼
                           ┌────────────────────────────────────────────┐
                           │        Page Allocation Needed              │
                           │   (alloc_pages() / __alloc_pages())        │
                           └────────────────────────────────────────────┘
                                              │
                                              ▼
                 ┌────────────────────────────────────────────────────────┐
                 │           PAGE ALLOCATOR CORE (mm/page_alloc.c)        │
                 ├────────────────────────────────────────────────────────┤
                 │ __alloc_pages()                                        │
                 │   → get_page_from_freelist()                           │
                 │     → select ZONE via zonelist                         │
                 │     → check watermark, per-cpu list                    │
                 │     → if pcp empty → buddy allocator                   │
                 │     → split/merge blocks (free_area[])                 │
                 └────────────────────────────────────────────────────────┘
                                              │
                                              ▼
                           ┌────────────────────────────────────────────┐
                           │         ZONES (mm/mmzone.c)               │
                           │ ├── DMA                                   │
                           │ ├── NORMAL                                │
                           │ └── HIGHMEM (32-bit)                      │
                           │ Each zone has:                            │
                           │   • free_area[order] lists                │
                           │   • watermarks                            │
                           │   • LRU (active/inactive)                 │
                           │   • per-cpu pagesets                      │
                           └────────────────────────────────────────────┘
                                              │
                                              ▼
                    ┌──────────────────────────────────────────────────────────┐
                    │        struct page (mem_map array)                       │
                    │ Each physical frame represented as struct page           │
                    │ Contains flags, refcount, mapping, index, lru, etc.      │
                    └──────────────────────────────────────────────────────────┘
                                              │
                                              ▼
                              Page returned to higher layer (VMA, cache)
                                              │
                                              ▼
  ┌────────────────────────────────────────────────────────────────────────────┐
  │        HIGHER-LEVEL ALLOCATORS (slab, kmalloc, vmalloc, page cache)        │
  ├────────────────────────────────────────────────────────────────────────────┤
  │ kmalloc(size) → kmem_cache_alloc() → SLAB allocator (mm/slab.c)            │
  │   → uses alloc_pages() to get backing pages                                │
  │                                                                            │
  │ vmalloc(size) → alloc_pages() + map to kernel virtual address (mm/vmalloc.c)│
  │                                                                            │
  │ filemap.c → page cache (add_to_page_cache())                               │
  │   → uses alloc_pages() for file-backed pages                               │
  │                                                                            │
  │ anonymous fault → alloc_pages() → LRU active list                          │
  └────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
                      ┌─────────────────────────────────────────────┐
                      │             PAGE CACHE / LRU                │
                      │  (mm/filemap.c, mm/vmscan.c, mm/rmap.c)     │
                      │                                             │
                      │  • LRU_ACTIVE / LRU_INACTIVE lists per zone │
                      │  • kswapd reclaims from inactive list       │
                      │  • dirty → writeback (page-writeback.c)     │
                      │  • swap-out via swap.c                      │
                      └─────────────────────────────────────────────┘
                                              │
                                              ▼
                ┌────────────────────────────────────────────────────┐
                │         RECLAIM PATH (vmscan.c + kswapd)           │
                │                                                    │
                │  if zone < watermark:                              │
                │   → wakeup_kswapd(zone)                            │
                │   → scan inactive list                             │
                │     → drop clean pages                             │
                │     → write dirty via pdflush                      │
                │     → unmap anon pages via rmap.c                  │
                │     → free_pages() → buddy allocator               │
                └────────────────────────────────────────────────────┘
                                              │
                                              ▼
                 ┌──────────────────────────────────────────────┐
                 │               SWAP SYSTEM                    │
                 │   swap.c / swap_state.c / swapfile.c         │
                 │   page_io.c → read/write swap device         │
                 │   reclaim sends anonymous pages to swap      │
                 └──────────────────────────────────────────────┘
                                              │
                                              ▼
                   ┌────────────────────────────────────────────┐
                   │          MEMORY PRESSURE HANDLING           │
                   │ oom_kill.c → kill process if reclaim fails  │
                   └────────────────────────────────────────────┘



# Conceptual Layers 
──────────────────────────────────────────────────────────────
USERSPACE
──────────────────────────────────────────────────────────────
 malloc(), mmap(), brk(), read(), write()

──────────────────────────────────────────────────────────────
SYSCALL & VMA LAYER (mm/mmap.c, mm/memory.c)
──────────────────────────────────────────────────────────────
 do_mmap(), do_page_fault(), handle_mm_fault()

──────────────────────────────────────────────────────────────
ALLOCATOR LAYER
──────────────────────────────────────────────────────────────
 kmalloc/slab.c     → for kernel objects
 vmalloc/vmalloc.c  → non-contiguous kernel memory
 page_alloc.c       → core buddy allocator
 bootmem.c          → early allocator
 mempool.c          → emergency pools

──────────────────────────────────────────────────────────────
PHYSICAL MEMORY LAYER
──────────────────────────────────────────────────────────────
 zones/mmzone.c  → DMA / NORMAL / HIGHMEM
 struct page[]   → page descriptors
 pg_data_t[]     → per-node data

──────────────────────────────────────────────────────────────
CACHING & RECLAIM
──────────────────────────────────────────────────────────────
 page cache / filemap.c
 vmscan.c / kswapd / reclaim
 page-writeback.c / pdflush
 swap.c / swap_state.c / page_io.c

──────────────────────────────────────────────────────────────
MONITORING / CONTROL
──────────────────────────────────────────────────────────────
 vmstat.c, oom_kill.c, thrash.c, migrate.c
──────────────────────────────────────────────────────────────
