/*
 * Linux 2.6.20 — mm/vmscan.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (page reclaim engine)
 *  - LRU lists (active/inactive)
 *  - kswapd vs direct reclaim
 *  - shrink_page_list() core logic
 *  - page lifecycle: mapped → writeback → free
 *  - slab reclaim (shrink_slab)
 *  - zone reclaim + NUMA
 *
 * Source: user-provided vmscan.c :contentReference[oaicite:0]{index=0}
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
This file is the HEART of Linux memory reclaim.

It answers:

    "When memory is low → how do we free pages?"

Core responsibilities:

    - scan LRU lists
    - decide which pages to evict
    - write dirty pages to disk
    - free clean pages
    - coordinate slab reclaim
*/


/*
Mental model:

    RAM full
      ↓
    reclaim needed
      ↓
    scan LRU
      ↓
    pick victim pages
      ↓
    write / swap / free
*/


/***************************************************************
 * 1. LRU MODEL (VERY IMPORTANT)
 ***************************************************************/

/*
Each zone maintains:

    active_list   → recently used pages
    inactive_list → reclaim candidates
*/


/*
Page lifecycle:

    alloc → active → inactive → reclaim
*/


/***************************************************************
 * 2. scan_control (RECLAIM CONTEXT)
 ***************************************************************/

/*
struct scan_control:

    nr_scanned        → how many pages examined
    gfp_mask          → allocation context
    may_writepage     → can we write dirty pages?
    may_swap          → can we swap?
    swappiness        → file vs anon preference
*/


/***************************************************************
 * 3. SHRINKERS (SLAB RECLAIM)
 ***************************************************************/

/*
Kernel caches (dentry, inode, etc.) are not page cache.

They use:

    shrinker callbacks
*/


/*
shrink_slab():

    for each shrinker:
        → ask: how many objects?
        → free some objects
*/


/*
IMPORTANT:

page reclaim + slab reclaim are balanced together
*/


/***************************************************************
 * 4. CORE FUNCTION: shrink_page_list() (CRITICAL)
 ***************************************************************/

/*
This is THE most important function.

Input:
    list of pages

Output:
    number of pages freed
*/


/*
Flow per page:

1. lock page
2. check if reclaimable
3. if mapped → try_to_unmap()
4. if dirty → writeback
5. if buffers → release
6. remove_mapping()
7. free page
*/


/***************************************************************
 * 5. PAGE DECISION TREE (VERY IMPORTANT)
 ***************************************************************/

/*
For each page:

    if mapped:
        try_to_unmap()

    if referenced:
        move to active (keep)

    if dirty:
        pageout()

    if clean:
        remove_mapping()

    if success:
        free page
*/


/***************************************************************
 * 6. pageout() (WRITEBACK LOGIC)
 ***************************************************************/

/*
Handles dirty pages.

Steps:

    clear_page_dirty_for_io()
    → mapping->writepage()

Outcome:

    PAGE_SUCCESS
    PAGE_ACTIVATE
    PAGE_KEEP
*/


/*
Key idea:

    reclaim prefers CLEAN pages
    DIRTY pages need I/O
*/


/***************************************************************
 * 7. remove_mapping() (FINAL FREE STEP)
 ***************************************************************/

/*
Removes page from page cache.

Conditions:

    page_count == 2
    !PageDirty
*/


/*
Flow:

    remove from radix tree
    drop page ref
*/


/***************************************************************
 * 8. shrink_inactive_list()
 ***************************************************************/

/*
1. isolate pages from inactive LRU
2. call shrink_page_list()
3. put back survivors
*/


/***************************************************************
 * 9. shrink_active_list()
 ***************************************************************/

/*
Moves pages:

    active → inactive

based on:

    page_referenced()
    swappiness
*/


/*
Goal:

    push cold pages toward reclaim
*/


/***************************************************************
 * 10. shrink_zone()
 ***************************************************************/

/*
Per-zone reclaim.

Flow:

    shrink_active_list()
    shrink_inactive_list()
*/


/***************************************************************
 * 11. DIRECT RECLAIM (CRITICAL)
 ***************************************************************/

/*
try_to_free_pages()

Called when:

    kmalloc / alloc_pages fails
*/


/*
Flow:

for priority = high → low:

    shrink_zones()
    shrink_slab()

until enough pages freed
*/


/***************************************************************
 * 12. KSWAPD (BACKGROUND RECLAIM)
 ***************************************************************/

/*
kswapd thread:

    runs in background
*/


/*
Flow:

while true:

    sleep
    wake on low memory
    balance_pgdat()
*/


/***************************************************************
 * 13. balance_pgdat() (CORE LOOP)
 ***************************************************************/

/*
This is kswapd main loop.

Steps:

1. find zones below watermark
2. scan zones
3. reclaim pages
4. repeat until healthy
*/


/***************************************************************
 * 14. WATERMARKS (IMPORTANT)
 ***************************************************************/

/*
Each zone has:

    pages_min
    pages_low
    pages_high
*/


/*
Trigger:

    free_pages < pages_low → wake kswapd
*/


/***************************************************************
 * 15. SWAPPINESS (IMPORTANT)
 ***************************************************************/

/*
vm_swappiness (0–100):

    0   → prefer file cache
    100 → prefer swapping anon
*/


/***************************************************************
 * 16. ISOLATE LRU PAGES
 ***************************************************************/

/*
isolate_lru_pages():

    removes pages from LRU
    puts into temporary list
*/


/*
Why?

    avoid holding zone lock during heavy work
*/


/***************************************************************
 * 17. WRITEBACK + CONGESTION
 ***************************************************************/

/*
If too many dirty pages:

    → wakeup_pdflush()
    → congestion_wait()
*/


/***************************************************************
 * 18. RECLAIM FLOW (END-TO-END)
 ***************************************************************/

/*
System low memory
    ↓
wakeup_kswapd()
    ↓
balance_pgdat()
    ↓
shrink_zone()
    ↓
shrink_inactive_list()
    ↓
shrink_page_list()
    ↓
pageout() / remove_mapping()
    ↓
page freed
*/


/***************************************************************
 * 19. KERNEL THREAD FLAGS (IMPORTANT)
 ***************************************************************/

/*
kswapd sets:

    PF_MEMALLOC
    PF_SWAPWRITE
    PF_KSWAPD
*/


/*
Meaning:

    privileged reclaim thread
*/


/***************************************************************
 * 20. NUMA + ZONE RECLAIM
 ***************************************************************/

/*
zone_reclaim():

    try local reclaim before remote allocation
*/


/***************************************************************
 * 21. KEY INSIGHT (VERY IMPORTANT)
 ***************************************************************/

/*
vmscan = "decision engine for page eviction"

It decides:

    WHICH page
    WHEN
    HOW (free vs write vs swap)
*/


/***************************************************************
 * 22. INTERVIEW MODEL
 ***************************************************************/

/*
mm/vmscan.c implements the Linux page reclaim subsystem. It scans LRU
lists, selects victim pages, writes back dirty pages, unmaps mapped
pages, and frees memory. It supports both direct reclaim and background
reclaim via kswapd, and balances page cache vs slab reclaim.
*/


/***************************************************************
 * 23. ONE-LINE SUMMARY
 ***************************************************************/

/*
vmscan.c is the core memory reclaim engine that scans LRU lists and
frees pages via unmapping, writeback, and eviction.
*/


/***************************************************************
 * END
 ***************************************************************/
