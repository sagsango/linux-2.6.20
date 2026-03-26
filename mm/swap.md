/*
 * Linux 2.6.20 — mm/swap.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (LRU, page lifecycle)
 *  - Page reference counting and freeing
 *  - LRU lists (active/inactive)
 *  - pagevec batching (CRITICAL optimization)
 *  - mark_page_accessed() aging logic
 *  - release_pages() and page freeing
 *  - Relation to vmscan, reclaim, swap_state
 *
 * Source: user-provided swap.c fileciteturn11file0
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
This file manages:

    - Page reference counting (put_page)
    - LRU lists (active/inactive)
    - Page lifecycle transitions
    - Batched page operations (pagevec)

This is NOT swap device logic.

Despite the name "swap.c", this file is really about:

    PAGE RECLAIM INFRASTRUCTURE + LRU MANAGEMENT

Think of it as:

    "Who owns the page and when can it be freed?"
*/


/*
Position in MM stack:

    page fault / alloc
        ↓
    page used (mark_page_accessed)
        ↓
    LRU lists (active/inactive)
        ↓
    reclaim (vmscan)
        ↓
    swap_state / page_io
*/


/***************************************************************
 * 1. CORE CONCEPT: PAGE LIFECYCLE
 ***************************************************************/

/*
A page goes through states:

    allocated
        ↓
    used (referenced)
        ↓
    LRU (inactive)
        ↓
    LRU (active)
        ↓
    reclaim candidate
        ↓
    freed OR swapped
*/


/*
This file controls transitions like:

    - mark accessed
    - activate page
    - move in LRU lists
    - release page
*/


/***************************************************************
 * 2. put_page() (VERY IMPORTANT)
 ***************************************************************/

/*
Function:

    put_page(struct page *page)

Purpose:

    decrement page reference count

Flow:

if compound page:
    handle specially
else if refcount reaches zero:
    __page_cache_release(page)
*/


/*
Meaning:

    When last reference goes away → page is freed
*/


/***************************************************************
 * 3. __page_cache_release()
 ***************************************************************/

/*
This is the REAL free path for normal pages
*/


/*
Flow:

1. if page is on LRU:
       remove from LRU list (zone->lru_lock)

2. free_hot_page(page)
*/


/*
Important:

    Pages must be removed from LRU before freeing
*/


/***************************************************************
 * 4. LRU STRUCTURE (VERY IMPORTANT)
 ***************************************************************/

/*
Each zone maintains:

    active_list
    inactive_list

Page flags:

    PageLRU
    PageActive
*/


/*
States:

    inactive list  → candidates for reclaim
    active list    → recently used
*/


/***************************************************************
 * 5. mark_page_accessed() (CRITICAL)
 ***************************************************************/

/*
Implements page aging logic
*/


/*
Transitions:

inactive,unreferenced  → inactive,referenced
inactive,referenced    → active
active,unreferenced    → active,referenced
*/


/*
Flow:

if (!PageActive && PageReferenced && PageLRU):
    activate_page()
    ClearPageReferenced
else if (!PageReferenced):
    SetPageReferenced
*/


/*
Meaning:

Second access promotes page to active list
*/


/***************************************************************
 * 6. activate_page()
 ***************************************************************/

/*
Moves page from inactive → active list
*/


/*
Flow:

1. lock zone->lru_lock
2. remove from inactive list
3. SetPageActive
4. add to active list
*/


/***************************************************************
 * 7. rotate_reclaimable_page()
 ***************************************************************/

/*
Used when writeback finishes
*/


/*
Flow:

if reclaimable:
    move page to tail of inactive list
    clear writeback flag
*/


/*
Meaning:

Give page another chance before reclaim
*/


/***************************************************************
 * 8. PAGEVEC (VERY IMPORTANT OPTIMIZATION)
 ***************************************************************/

/*
struct pagevec = small array of pages

Used for batching operations
*/


/*
Why?

    avoid frequent locking
    improve cache locality
*/


/***************************************************************
 * 9. lru_cache_add()
 ***************************************************************/

/*
Adds page to LRU via per-CPU pagevec
*/


/*
Flow:

1. get per-CPU pagevec
2. add page
3. if full → flush (__pagevec_lru_add)
*/


/***************************************************************
 * 10. __pagevec_lru_add()
 ***************************************************************/

/*
Actual insertion into LRU
*/


/*
Flow:

for each page:
    SetPageLRU
    add to inactive list

then release references
*/


/***************************************************************
 * 11. ACTIVE PAGE ADD
 ***************************************************************/

/*
__pagevec_lru_add_active()

Adds directly to active list
*/


/***************************************************************
 * 12. release_pages() (VERY IMPORTANT)
 ***************************************************************/

/*
Batch free function
*/


/*
Flow:

for each page:
    if refcount == 0:
        if on LRU:
            remove from LRU
        add to free list

finally:
    free pages via pagevec
*/


/*
Important race handling:

Re-check page state under lock to avoid races with reclaim
*/


/***************************************************************
 * 13. __pagevec_release()
 ***************************************************************/

/*
Release a pagevec
*/


/*
Flow:

1. lru_add_drain()
2. release_pages()
*/


/***************************************************************
 * 14. lru_add_drain()
 ***************************************************************/

/*
Flush per-CPU pagevecs to global LRU
*/


/*
Why?

    ensure pages are visible globally before free/reclaim
*/


/***************************************************************
 * 15. pagevec_lookup()
 ***************************************************************/

/*
Batch lookup of pages from mapping
*/


/*
Uses:

    find_get_pages()
*/


/***************************************************************
 * 16. MEMORY ACCOUNTING
 ***************************************************************/

/*
vm_acct_memory()

Tracks committed memory per CPU
*/


/*
Uses per-CPU counters to reduce contention
*/


/***************************************************************
 * 17. CPU HOTPLUG INTERACTION
 ***************************************************************/

/*
On CPU removal:

    flush per-CPU committed_space
    drain LRU pagevecs
*/


/***************************************************************
 * 18. swap_setup()
 ***************************************************************/

/*
Initializes swap subsystem parameters
*/


/*
Key:

page_cluster =
    small RAM → smaller cluster
    large RAM → larger cluster
*/


/***************************************************************
 * 19. FULL FLOW (END-TO-END)
 ***************************************************************/

/*
Page usage:

page allocated
    ↓
mark_page_accessed()
    ↓
possibly activate_page()
    ↓
placed in LRU

Reclaim:

vmscan selects page
    ↓
if clean → free
    ↓
if dirty → writeback
    ↓
possibly swap (swap_state.c)

Free:

put_page()
    ↓
refcount == 0
    ↓
__page_cache_release()
    ↓
remove from LRU
    ↓
free_hot_page()
*/


/***************************************************************
 * 20. CONNECTION TO OTHER FILES
 ***************************************************************/

/*
Works with:

vmscan.c:
    decides which pages to reclaim

swap_state.c:
    swap cache management

page_io.c:
    swap I/O

rmap.c:
    reverse mapping for reclaim
*/


/***************************************************************
 * 21. KEY INSIGHT (VERY IMPORTANT)
 ***************************************************************/

/*
This file is about:

    WHO OWNS A PAGE
    WHEN IT CAN BE FREED

and

    HOW PAGES MOVE THROUGH LRU STATES
*/


/***************************************************************
 * 22. INTERVIEW MODEL
 ***************************************************************/

/*
mm/swap.c implements page lifecycle and LRU management.

It handles:
    - reference counting (put_page)
    - page activation and aging
    - LRU list transitions
    - batched page operations
*/


/***************************************************************
 * 23. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/swap.c manages page lifecycle, LRU lists, and batched page operations,
forming the core infrastructure for page reclaim and memory management.
*/


/***************************************************************
 * END
 ***************************************************************/

