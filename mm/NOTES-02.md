# Summary
| Layer                    | Key Files                                | Core Responsibility        |
| ------------------------ | ---------------------------------------- | -------------------------- |
| **User → Kernel**        | `mmap.c`, `memory.c`                     | Manage VMAs and syscalls   |
| **Allocator Core**       | `page_alloc.c`, `mmzone.c`, `bootmem.c`  | Physical page allocation   |
| **SLAB Layer**           | `slab.c`, `slob.c`                       | Object caches for kmalloc  |
| **Virtual Kernel Space** | `vmalloc.c`, `highmem.c`                 | Virtual mapping of pages   |
| **Page Cache / FS**      | `filemap.c`, `truncate.c`, `readahead.c` | Cache file-backed pages    |
| **Reclaim**              | `vmscan.c`, `swap.c`, `rmap.c`           | Free memory under pressure |
| **Writeback**            | `page-writeback.c`, `pdflush.c`          | Write dirty pages to disk  |
| **NUMA/Policy**          | `mempolicy.c`, `memory_hotplug.c`        | Node-aware allocation      |
| **Debug/Stats**          | `vmstat.c`, `util.c`                     | Metrics and tools          |



# MASTER FLOW DIAGRAM
                          ┌───────────────────────────────────────┐
                          │           USER SPACE                  │
                          │───────────────────────────────────────│
                          │ malloc()   mmap()   brk()   new file  │
                          └───────────────┬───────────────────────┘
                                          │
                                          ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                   KERNEL MEMORY MANAGEMENT (mm/)                              │
└──────────────────────────────────────────────────────────────────────────────┘

══════════════════════════════════════════════════════════════════════════════════
1) ENTRY POINT: MEMORY REQUEST
══════════════════════════════════════════════════════════════════════════════════
                       kmalloc()     vmalloc()
                           │            │
             ┌─────────────┘            └──────────────┐
             ▼                                          ▼
    (SLAB / SLOB allocator)                       (vmalloc.c)
          slab.c                                       |
          slob.c                                       |
             │                                          |
             └─────────────► needs pages? ◄─────────────┘
                             │ YES
                             ▼

══════════════════════════════════════════════════════════════════════════════════
2) PHYSICAL PAGE ALLOCATION PATH
══════════════════════════════════════════════════════════════════════════════════
                        alloc_pages(gfp, order)
                                │
                      ┌─────────┴───────────────────────────┐
                      ▼                                     ▼
              Find ZONELIST                           Watermark Check
            (mmzone.c: zonelist)                     (zone->watermark)
                      │                                     │
                      ▼                                     │
          get_page_from_freelist()                          │
           (page_alloc.c)                                   │
                      │                                     ▼
                      │                            Not enough memory?
                      │                                     │
                      ▼                        ┌────────────┴────────────┐
              PER-CPU PAGE CACHE               │                         │
                (fast path)                    ▼                         ▼
                      │                 Direct Reclaim             kswapd waking
          page_alloc.c: pcp lists           (vmscan.c)             (vmscan.c)
                      │                        │                           │
         miss? (slow path)                     │                           │
                      │                        ▼                           │
                      ▼                  reclaim pages                     │
              BUDDY ALLOCATOR          (drop cache, swap)                  │
            (page_alloc.c)                    │                            │
         ♦ find free area by order            │                            │
         ♦ split higher orders                ▼                            │
         ♦ remove page from list      pages become free  ◄─────────────────┘
                      │
                      ▼
        SUCCESS: return struct page *

══════════════════════════════════════════════════════════════════════════════════
3) SLAB ALLOCATORS (kmalloc path)
══════════════════════════════════════════════════════════════════════════════════
 kmalloc(size)
      │
      ▼
 select SLAB cache (kmalloc-XX)
      │
      ▼
 cache empty?
      │ YES
      ▼
 allocate new slab → uses alloc_pages()  (back to step 2)
      │
      ▼
 return pointer to object

══════════════════════════════════════════════════════════════════════════════════
4) VIRTUAL MEMORY MAP (mmap/brk path)
══════════════════════════════════════════════════════════════════════════════════

mmap() / brk()  ───────────────────►  mmap.c
                                        │
                                        ▼
                             Create / extend VMA
                               (vm_area_struct)
                                        │
                                        ▼
                    page fault occurs when accessed by user
                                        │
                                        ▼
                             do_page_fault() → handle_mm_fault()
                                        │
                                        ▼
                Anonymous?                     File-backed?
             (anonymous memory)                (page cache)
                    │                                 │
                    ▼                                 ▼
        alloc_pages() for anonymous        filemap.c: find page in page cache?
                    │                                 │
                    ▼                                 ▼
         map page into page tables          miss? → allocate page + read from disk
                    │                                 │
                    ▼                                 ▼
             return to userspace           map page into page tables

══════════════════════════════════════════════════════════════════════════════════
5) PAGE CACHE FLOW (file-backed memory)
══════════════════════════════════════════════════════════════════════════════════
                      Read / Write / Fault
                                 │
                                 ▼
                          filemap.c
                   (radix-tree page cache)
                                 │
                  page present?  │  no?
                                 │  ▼
                                 │ alloc_pages() + disk read
                                 │ add_to_page_cache()
                                 │
                                 ▼
                    Return cached page to VFS

══════════════════════════════════════════════════════════════════════════════════
6) DIRTY PAGES + WRITEBACK
══════════════════════════════════════════════════════════════════════════════════
                         Page becomes dirty
                                 │
                                 ▼
                         page-writeback.c
                                 │
                                 ▼
                balance_dirty_pages()  (throttling)
                                 │
                                 ▼
                pdflush thread or kswapd writeback
                          (pdflush.c)

══════════════════════════════════════════════════════════════════════════════════
7) MEMORY RECLAIM (vmscan)
══════════════════════════════════════════════════════════════════════════════════
                       Memory low?
                           │
                           ▼
                     vmscan.c runs:
       ┌───────────────────────────────────────────────────────────┐
       │  1. scan LRU (active → inactive)                          │
       │  2. try_to_unmap() (rmap.c)                               │
       │  3. drop clean pages                                      │
       │  4. writeback dirty pages                                 │
       │  5. swap out anonymous memory (swap.c)                    │
       └───────────────────────────────────────────────────────────┘
                           │
                           ▼
                   pages freed → buddy allocator

══════════════════════════════════════════════════════════════════════════════════
8) SWAP SYSTEM FLOW
══════════════════════════════════════════════════════════════════════════════════
swap.c / swapfile.c / swap_state.c
          │
          ▼
  allocate swap slot
          │
          ▼
write page to swap (page_io.c)
          │
          ▼
mark pte as swapped (not present)
          │
          ▼
   page freed → buddy allocator

══════════════════════════════════════════════════════════════════════════════════
9) OOM KILLER
══════════════════════════════════════════════════════════════════════════════════
If reclaim fails repeatedly:

      OOM KILLER (oom_kill.c)
            │
            ▼
 Select worst process
            │
            ▼
 Kill it → free all memory


