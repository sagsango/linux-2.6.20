/*
 * Linux 2.6.20 — mm/page-writeback.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - Background (deep)
 *  - Flow
 *  - Core concepts
 *  - Function walkthrough
 *  - Relation to page_io.c (swap)
 *
 * ------------------------------------------------------------
 */


/***************************************************************
 * 0. BIG PICTURE
 ***************************************************************

/*
    User write() / mmap write
            |
            v
    Page Cache (struct page)
            |
            v
    Page becomes DIRTY (set_page_dirty)
            |
            v
    THIS FILE (page-writeback.c)
        - controls dirty limits
        - triggers writeback
        - throttles writers
            |
            v
    Filesystem (writepage)
            |
            v
    Block Layer (BIO)
            |
            v
    Disk / Storage
*/


/***************************************************************
 * 1. WHY THIS FILE EXISTS
 ***************************************************************

/*
Problem:
    - Processes modify memory
    - Pages become DIRTY
    - Dirty pages accumulate in RAM

If uncontrolled:
    - RAM fills with dirty pages
    - Huge I/O bursts later
    - System stalls / OOM / latency spikes

Solution:
    - Limit dirty memory
    - Gradually flush dirty pages
    - Throttle writers when needed
*/


/***************************************************************
 * 2. CORE CONCEPTS
 ***************************************************************

/*
2.1 DIRTY PAGE
----------------
Page in RAM != disk content

Set by:
    set_page_dirty()

Tracked via:
    PageDirty flag
    radix tree tags (PAGECACHE_TAG_DIRTY)
*/


/*
2.2 WRITEBACK
----------------
DIRTY → WRITEBACK → CLEAN

Flags:
    PageDirty
    PageWriteback
*/


/*
2.3 address_space
----------------
Represents file mapping

Contains:
    - radix tree of pages
    - writeback index

All writeback happens via address_space
*/


/*
2.4 writeback_control (wbc)
---------------------------
Key fields:

    nr_to_write       → how many pages to write
    sync_mode         → WB_SYNC_NONE / WB_SYNC_ALL
    range_cyclic      → circular scan
    nonblocking       → avoid congestion
    older_than_this   → time-based flush
*/


/*
2.5 DIRTY LIMITS
----------------

vm_dirty_ratio             (default ~40%)
dirty_background_ratio     (default ~10%)

Derived:

    dirty_thresh      → max dirty pages allowed
    background_thresh → start background writeback
*/


/***************************************************************
 * 3. RESPONSIBILITIES OF THIS FILE
 ***************************************************************

/*
(A) Control dirty memory
    - throttle writers
    - enforce limits

(B) Perform writeback
    - scan dirty pages
    - invoke filesystem writepage
*/


/***************************************************************
 * 4. END-TO-END FLOW (VERY IMPORTANT)
 ***************************************************************

/*
write() / mmap write
        ↓
set_page_dirty()
        ↓
balance_dirty_pages_ratelimited()
        ↓
balance_dirty_pages()
        ↓
    (if over limit)
        ↓
    writeback_inodes()
        ↓
    generic_writepages()
        ↓
    mapping->a_ops->writepage()
        ↓
    BIO → Disk
        ↓
clear_page_dirty_for_io()
        ↓
Page becomes CLEAN
*/


/***************************************************************
 * 5. DIRTY THROTTLING
 ***************************************************************

/*
Function:
    balance_dirty_pages()

Goal:
    Prevent excessive dirty memory
*/


/*
Core loop:

    get_dirty_limits()

    nr_dirty = NR_FILE_DIRTY + NR_UNSTABLE_NFS

    if nr_dirty <= dirty_thresh:
        break

    writeback_inodes()

    if still high:
        congestion_wait()
*/


/*
IMPORTANT INSIGHT:

    Writers themselves perform writeback
*/


/*
Ratelimiting:

    balance_dirty_pages_ratelimited_nr()

    - uses per-CPU counters
    - avoids expensive checks on every page dirty
*/


/***************************************************************
 * 6. BACKGROUND WRITEBACK
 ***************************************************************

/*
Triggered when:

    dirty > background_thresh

via:

    pdflush_operation(background_writeout)
*/


/*
Function:
    background_writeout()

Loop:

    while dirty > background_thresh:
        write MAX_WRITEBACK_PAGES
        if congestion:
            wait
*/


/***************************************************************
 * 7. PERIODIC WRITEBACK (KUPDATE)
 ***************************************************************

/*
Timers:

    dirty_writeback_interval = 5 * HZ
    dirty_expire_interval    = 30 * HZ
*/


/*
Function:
    wb_kupdate()

Purpose:
    Flush OLD dirty pages

Condition:
    page_age > dirty_expire_interval
*/


/***************************************************************
 * 8. LAPTOP MODE
 ***************************************************************

/*
Goal:
    Reduce disk wakeups

Strategy:
    - delay writes
    - flush all at once
*/


/*
Functions:

    laptop_io_completion()
    laptop_timer_fn()
*/


/***************************************************************
 * 9. CORE WRITEBACK ENGINE
 ***************************************************************

/*
Function:
    generic_writepages()

Flow:

for each dirty page:
    lock_page()

    if PageWriteback:
        skip

    clear_page_dirty_for_io()

    call writepage()

    decrement nr_to_write
*/


/*
Optimizations:

    - pagevec batching
    - cyclic scanning (writeback_index)
    - skip pages already in IO
*/


/***************************************************************
 * 10. DIRTY BIT HANDLING (SUBTLE)
 ***************************************************************

/*
set_page_dirty():
    - marks page dirty
    - updates radix tree
    - updates accounting
*/


/*
clear_page_dirty_for_io():

    page_mkclean()
    set_page_dirty()   // for side effects
    TestClearPageDirty()
    decrement stats

Insight:
    dirty bit acts as serialization point
*/


/***************************************************************
 * 11. WRITEBACK STATE MACHINE
 ***************************************************************

/*
DIRTY → WRITEBACK → CLEAN
*/


/*
Functions:

    test_set_page_writeback()
    test_clear_page_writeback()
*/


/***************************************************************
 * 12. FILESYSTEM INTERACTION
 ***************************************************************

/*
Kernel does NOT write disk directly

Calls:

    mapping->a_ops->writepage()
    mapping->a_ops->writepages()

Filesystem decides:
    - block mapping
    - journaling
*/


/***************************************************************
 * 13. RELATION TO page_io.c (SWAP)
 ***************************************************************

/*
File-backed pages:
    → page-writeback.c
    → write to file

Anonymous pages:
    → page_io.c
    → write to swap
*/


/***************************************************************
 * 14. PDFLUSH THREADS
 ***************************************************************

/*
Kernel worker threads for writeback

Used via:

    pdflush_operation(func, arg)
*/


/***************************************************************
 * 15. PERFORMANCE DESIGN INSIGHTS
 ***************************************************************

/*
1. Avoid bursts
    → thresholds + background writeback

2. Avoid starvation
    → throttling

3. Reduce contention
    → batching (pagevec)

4. Reduce disk wakeups
    → laptop mode
*/


/***************************************************************
 * 16. IMPORTANT VARIABLES
 ***************************************************************

/*
 dirty_background_ratio
 vm_dirty_ratio
 dirty_writeback_interval
 dirty_expire_interval
 ratelimit_pages
 dirty_exceeded
*/


/***************************************************************
 * 17. MENTAL MODEL (INTERVIEW)
 ***************************************************************

/*
write() → page cache → dirty page

Kernel monitors dirty %

if too high:
    - throttle writer
    - trigger writeback

writeback:
    → filesystem writepage()
    → disk
*/


/***************************************************************
 * 18. DEBUGGING INSIGHTS
 ***************************************************************

/*
System slow during writes?
    → balance_dirty_pages throttling

Disk spikes?
    → background_writeout

fsync slow?
    → WB_SYNC_ALL (wait for IO)

Memory pressure + swap?
    → writeback too slow
*/


/***************************************************************
 * 19. HOW TO READ THIS FILE
 ***************************************************************

/*
Start from:

    balance_dirty_pages()
    background_writeout()
    generic_writepages()
    clear_page_dirty_for_io()

Then connect:

    address_space
    page flags
    filesystem writepage
    block layer
*/


/***************************************************************
 * END
 ***************************************************************/

