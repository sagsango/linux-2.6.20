# vmscan.c: Very complex task


# CLEAN MASTER DIAGRAM OF MEMORY RECLAIM (vmscan.c, Linux 2.6.20)
====================================================================
      MEMORY LOW → PAGE RECLAIM PIPELINE OVERVIEW (vmscan.c)
====================================================================

                   LOW FREE MEMORY?
                           │
             ┌─────────────┴─────────────┐
             │                           │
   Direct Reclaim (Allocating task)     kswapd (Background daemon)
      try_to_free_pages()                wakeup_kswapd()
             │                           │
             └──────────────┬────────────┘
                            ▼
                      balance_pgdat()
                            │
                 Scan zones from HIGH→LOW
                            │
                            ▼
                    shrink_zone()
          (scan both ACTIVE and INACTIVE lists)
                            │
              ┌─────────────┴──────────────┐
              ▼                              ▼
   shrink_active_list()           shrink_inactive_list()
 (move some pages → inactive)   (actually try to free pages)
              │                              │
              └──────────────┬───────────────┘
                             ▼
                      shrink_page_list()
         (per-page reclaim: unmap, writeback, swap-out, free)
                             │
                             ▼
                     FREE PAGE → Buddy System


# PART 2 — ACTIVE → INACTIVE SCANNING FLOW 
=======================================================
      shrink_active_list() — ACTIVE LIST PROCESSING
=======================================================

Input: number of pages to scan from zone->active_list

shrink_active_list():
        │
        ▼
1. isolate_lru_pages()      ← remove pages from ACTIVE list
        │
        ▼
2. For each isolated page:
        │
        ├─ Is page mapped?
        │     ├─ If referenced → KEEP ACTIVE
        │     └─ If not referenced:
        │            ↓
        │         move to INACTIVE
        │
        ├─ If unmapped but referenced → KEEP ACTIVE
        │
        └─ Otherwise → move to INACTIVE list

Result:
   ACTIVE list: hot / recently used pages
   INACTIVE list: cold / reclaim candidates

# PART 3 — INACTIVE LIST RECLAIM FLOW
=======================================================
      shrink_inactive_list() — INACTIVE RECLAIM FRONT
=======================================================

shrink_inactive_list():
        │
        ▼
1. isolate_lru_pages()   ← pull pages from INACTIVE LRU
        │
        ▼
2. shrink_page_list()    ← decide what to do with each page
        │
        ├─ reclaim pages (free)
        └─ put back unfreeable pages

Returns: Number of pages reclaimed

# PART 4 — THE HEART OF RECLAIM: shrink_page_list()
===================================================================
                 shrink_page_list() — PER PAGE LOGIC
===================================================================

For each page in page_list:
        │
        ▼
   [1] Lock the page
        │
        ├─ If page is under writeback → KEEP
        ├─ If page cannot be swapped and is mapped → KEEP
        │
        ▼
   [2] Referenced?
        ├─ Yes → ACTIVATE the page (move back to active list)
        └─ No  → continue
        │
        ▼
   [3] Anonymous page?
        ├─ Yes + not in swapcache:
        │        → add_to_swap()
        │        → now reclaimable via swap
        └─ No → continue
        │
        ▼
   [4] Mapped page?
        ├─ Yes → try_to_unmap()
        │           ├─ FAIL → ACTIVATE (keep)
        │           ├─ AGAIN → keep_locked
        │           └─ SUCCESS → continue
        └─ No → continue
        │
        ▼
   [5] Dirty page?
        ├─ Yes → pageout():
        │          ├─ PAGE_KEEP → keep
        │          ├─ PAGE_ACTIVATE → activate
        │          ├─ PAGE_SUCCESS → proceed to free
        │          └─ PAGE_CLEAN → proceed to free
        └─ No → continue
        │
        ▼
   [6] Buffer heads?
        ├─ Yes → try_to_release_page()
        │          ├─ Fail → activate
        │          └─ Success → continue
        └─ No → continue
        │
        ▼
   [7] remove_mapping()
        ├─ SUCCESS → free page to buddy allocator
        └─ FAIL → keep page

Result:
    Reclaimable pages get unmapped → written out (if needed) → removed from
    page cache → freed.

# PART 5 — SWAP-OUT FLOW IN RECLAIM
===============================================
           Anonymous Page Swap-Out Path
===============================================

Anonymous page (PageAnon)
   │
   ├─ If !PageSwapCache
   │       ↓
   │   add_to_swap()
   │       ↓
   │   allocate swap slot
   │       ↓
   │   add to swapcache
   │
   │ → Now page behaves like file-backed dirty page
   │
   ▼
pageout()
   │
   ├─ Uses swapfile’s →writepage handler
   │
   ▼
write to swap device
   │
   ▼
remove_mapping()
   │
   ▼
free page → buddy allocator

# PART 6 — kswapd RECLAIM LOOP
==============================================
             kswapd main loop
==============================================

kswapd():
    │
    ▼
for (;;) {
    sleep until woken by:
         wakeup_kswapd(zone, order)
    │
    ▼
    read requested "order" (high-order alloc fails wake it)
    │
    ▼
    balance_pgdat(pgdat, order)
         │
         ├─ shrink_zones()
         ├─ shrink_slab()
         └─ bring zones back to pages_high watermark
}

# PART 7 — WHOLE SYSTEM OVERVIEW (ALL SUBSYSTEMS)
=====================================================================
          COMPLETE MEMORY RECLAIM PIPELINE (ALL STEPS TOGETHER)
=====================================================================

           LOW MEMORY
               │
               ▼
   try_to_free_pages()  OR  kswapd
               │
               ▼
         shrink_zones()
               │
               ▼
   ┌──────────────────────────────┬──────────────────────────────┐
   │ shrink_active_list()         │ shrink_inactive_list()       │
   └───────────────┬─────────────┴────────────┬──────────────────┘
                   ▼                          ▼
          ACTIVE pages "cooled" → INACTIVE    isolated pages to reclaim
                   │                          │
                   └──────────────┬───────────┘
                                  ▼
                         shrink_page_list()
                                  │
        ┌─────────────────────────┼──────────────────────────────┐
        ▼                         ▼                              ▼
   Unmap PTEs              Writeback dirty pages           Swap out anon pages
  (try_to_unmap)           (pageout → writepage)          (add_to_swap)
        │                         │                              │
        └──────────────┬──────────┴────────────┬─────────────────┘
                       ▼                       ▼
                remove_mapping()            buffers?
                       │                       │
                       ▼                       ▼
               finally free page → buddy allocator

