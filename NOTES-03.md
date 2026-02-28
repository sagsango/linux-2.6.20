```txt
================================================================================
                LINUX 2.6.20 — SWAP + PAGE FAULT + OOM
                    END-TO-END EXECUTION FLOW
================================================================================

Author: Kernel Walkthrough Mode
Scope : Anonymous memory, LRU reclaim, swap out, swap in, OOM
Kernel: Linux 2.6.20
================================================================================



###############################################################################
###############################################################################
##                                                                         ####
##                         PART 1 — SWAP OUT FLOW                          ####
##           (Memory Pressure → Reclaim → Page Written to Swap)            ####
##                                                                         ####
###############################################################################
###############################################################################


STEP 0 ─ MEMORY PRESSURE TRIGGER
---------------------------------

Cause:
    - alloc_pages() slow path
    - __alloc_pages() cannot find free pages
    - kswapd running in background

Entry Points:

    mm/page_alloc.c
        __alloc_pages()
            → try_to_free_pages()

OR

    mm/vmscan.c
        kswapd()
            → balance_pgdat()



STEP 1 ─ RECLAIM ENGINE (LRU SCANNING)
---------------------------------------

File:
    mm/vmscan.c

Core Call Flow:

    try_to_free_pages()
        → shrink_zones()
            → shrink_zone()
                → shrink_list()

Pages are taken from:

    Active LRU
    Inactive LRU

Anonymous pages → candidate for swap.



STEP 2 ─ PAGE SELECTED FOR RECLAIM
-----------------------------------

Inside shrink_list():

    → pageout(page)

File:
    mm/vmscan.c

Conditions:
    - Page is anonymous
    - Not mlocked
    - Not pinned
    - Writeable to swap



STEP 3 ─ ALLOCATE SWAP SLOT
----------------------------

pageout()
    → add_to_swap(page)

File:
    mm/swap_state.c

add_to_swap()
    → get_swap_page()

File:
    mm/swapfile.c

This updates:

    struct swap_info_struct
        → swap_map[slot]++

This is GLOBAL swap metadata.

Return:
    swp_entry_t (type + offset)



STEP 4 ─ UNMAP PAGE (PTE → SWAP ENTRY)
---------------------------------------

Reverse map walk:

    try_to_unmap()

File:
    mm/rmap.c

For every mapping:
    PTE (present) → PTE (swap entry)

Using:
    swp_entry_to_pte()

Now:
    Virtual memory still valid
    But PTE no longer points to RAM
    Instead points to swap slot



STEP 5 ─ WRITE PAGE TO SWAP DEVICE
-----------------------------------

pageout()
    → swap_writepage()

File:
    mm/page_io.c

Flow:
    → build BIO
    → submit_bio()
    → disk write
    → IO completion handler



STEP 6 ─ FREE PAGE FRAME
-------------------------

After write completes:

    - Page removed from LRU
    - Page freed to buddy allocator

Remaining Metadata:

    PTE contains swp_entry_t
    swap_map[slot] tracks usage

Memory successfully reclaimed.



###############################################################################
###############################################################################
##                                                                         ####
##                         PART 2 — SWAP IN FLOW                           ####
##              (Access → Page Fault → Read From Swap)                     ####
##                                                                         ####
###############################################################################
###############################################################################


STEP 0 ─ CPU PAGE FAULT
------------------------

Process accesses swapped address.

Hardware:
    - Present bit = 0
    - Page Fault exception (#PF)



STEP 1 ─ ARCH FAULT HANDLER
----------------------------

File (i386):
    arch/i386/mm/fault.c

Function:
    do_page_fault()

Calls:
    handle_mm_fault()



STEP 2 ─ GENERIC MM FAULT HANDLER
----------------------------------

File:
    mm/memory.c

Main Dispatcher:
    handle_mm_fault()
        → handle_pte_fault()

If:
    is_swap_pte(pte)
        → do_swap_page()



STEP 3 ─ SWAP PAGE HANDLER
---------------------------

File:
    mm/memory.c

Function:
    do_swap_page()

This is THE swap-in handler.



SUBSTEP A ─ EXTRACT SWAP ENTRY

    entry = pte_to_swp_entry(orig_pte)



SUBSTEP B ─ LOOKUP SWAP CACHE

File:
    mm/swap_state.c

Function:
    lookup_swap_cache(entry)

If present:
    Reuse page



SUBSTEP C ─ NOT IN CACHE → READ FROM SWAP

    read_swap_cache_async(entry)

File:
    mm/swap_state.c

Which calls:

    swap_readpage()

File:
    mm/page_io.c

Disk IO issued.
Page locked until completion.



SUBSTEP D ─ INSTALL PTE

After page ready:

    set_pte_at()

Replace:

    swap PTE → present PTE (physical frame)

Then:

    - add to LRU
    - update rmap
    - mark accessed
    - swap_free(entry) (if refcount zero)

Fault resolved.



###############################################################################
###############################################################################
##                                                                         ####
##                         PART 3 — OOM FLOW                               ####
##               (When Reclaim + Swap Cannot Free Memory)                  ####
##                                                                         ####
###############################################################################
###############################################################################


STEP 0 ─ ALLOCATION STILL FAILS
--------------------------------

In:

    __alloc_pages()

After:
    - Direct reclaim
    - kswapd
    - Swap exhausted

Then:

    out_of_memory()

File:
    mm/oom_kill.c



STEP 1 ─ SELECT VICTIM
----------------------

Function:
    select_bad_process()

Scoring factors:
    - total_vm
    - rss
    - oom_adj
    - privileges



STEP 2 ─ KILL PROCESS
----------------------

Function:
    oom_kill_process()

Sends:
    SIGKILL

Victim exits.



STEP 3 ─ MEMORY RECOVERY
-------------------------

Exit path:

    do_exit()
        → exit_mmap()
            → unmap_vmas()
            → swap_free()
            → free pages

Memory released.

OOM resolved.



###############################################################################
###############################################################################
##                        COMPLETE SYSTEM PICTURE                           ####
###############################################################################
###############################################################################

SWAP OUT:

    alloc fail
        ↓
    try_to_free_pages()
        ↓
    shrink_list()
        ↓
    pageout()
        ↓
    get_swap_page()
        ↓
    try_to_unmap()
        ↓
    swap_writepage()
        ↓
    free page frame


SWAP IN:

    CPU page fault
        ↓
    do_page_fault()
        ↓
    handle_mm_fault()
        ↓
    do_swap_page()
        ↓
    lookup_swap_cache()
        ↓
    swap_readpage()
        ↓
    install PTE


OOM:

    reclaim fails
        ↓
    out_of_memory()
        ↓
    select_bad_process()
        ↓
    SIGKILL
        ↓
    memory freed



###############################################################################
###############################################################################
##                         CORE DATA STRUCTURES                              ####
###############################################################################
###############################################################################

struct page
struct mm_struct
struct vm_area_struct
struct swap_info_struct
    → swap_map[]
swp_entry_t
PTE
LRU lists



###############################################################################
###############################################################################
##                            CRITICAL INSIGHTS                             ####
###############################################################################
###############################################################################

1. Swap metadata lives in THREE places:
       - PTE (swp_entry_t)
       - swap_map[] (global slot tracking)
       - swap cache (if resident)

2. do_swap_page() is the swapped-page fault handler.

3. pageout() initiates swap out.

4. OOM is triggered only AFTER:
       - LRU reclaim
       - Swap
       - Direct reclaim
       - kswapd
       - All fail.

5. swapfile.c owns swap slot truth.


================================================================================
                                END OF FILE
================================================================================
```

