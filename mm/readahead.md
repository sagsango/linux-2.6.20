/*
 * Linux 2.6.20 — mm/readahead.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - Background (VERY IMPORTANT for read path)
 *  - What readahead solves
 *  - Adaptive algorithm (core logic)
 *  - Window model (current + ahead)
 *  - Sequential vs random detection
 *  - Full flow to disk read
 *
 * Source: Linux 2.6.20 mm/readahead.c
 */


/***************************************************************
 * 0. BIG PICTURE
 ***************************************************************

/*
Problem:
    Disk is slow, RAM is fast.

    If application reads file sequentially:
        read(page 0)
        read(page 1)
        read(page 2)

    Without readahead:
        each read → disk IO → slow

Solution:
    Predict future reads and prefetch pages.

This is what readahead.c does.
*/


/*
Pipeline:

    read() / page fault
            |
            v
    page_cache_readahead()
            |
            v
    __do_page_cache_readahead()
            |
            v
    read_pages()
            |
            v
    mapping->a_ops->readpage(s)
            |
            v
    BIO → disk → pages filled
*/


/***************************************************************
 * 1. KEY DATA STRUCTURE
 ***************************************************************

struct file_ra_state {

    unsigned long start;       // current window start
    unsigned long size;        // current window size

    unsigned long ahead_start; // ahead window start
    unsigned long ahead_size;  // ahead window size

    unsigned long ra_pages;    // max readahead

    pgoff_t prev_page;         // last accessed page

    unsigned int flags;        // MISS / INCACHE

    unsigned long cache_hit;
};


/***************************************************************
 * 2. TWO-WINDOW MODEL (VERY IMPORTANT)
 ***************************************************************

/*
There are TWO windows:

    CURRENT WINDOW
    AHEAD WINDOW


Diagram:

    ----|-------------|-------------|------
        ^start        ^start+size
                     ^ahead_start  ^ahead_start+ahead_size


While app reads CURRENT:
    kernel performs IO for AHEAD

When CURRENT finishes:
    CURRENT ← AHEAD
    new AHEAD created
*/


/***************************************************************
 * 3. SEQUENTIAL DETECTION
 ***************************************************************

/*
sequential = (offset == prev_page + 1)

If NOT sequential:
    → disable readahead
    → treat as random IO
*/


/***************************************************************
 * 4. INITIAL WINDOW SIZE
 ***************************************************************

get_init_ra_size(size, max)

/*
Logic:
    small → grow aggressively
    medium → grow moderately
    large → clamp to max
*/


/***************************************************************
 * 5. WINDOW GROWTH (ADAPTIVE)
 ***************************************************************

get_next_ra_size(ra)

/*
If cache miss:
    shrink window

Else:
    if small → x4
    else      → x2

Bounded by max_readahead
*/


/***************************************************************
 * 6. CORE FUNCTION
 ***************************************************************

page_cache_readahead()

/*
Main entry point called on every read
*/


/*
Flow:

1. detect sequential access

2. if first sequential access:
       initialize window
       trigger IO

3. if random access:
       disable readahead
       do minimal IO

4. if sequential with existing window:
       if no ahead window:
            create ahead window

       if crossed into ahead window:
            shift windows
            create new ahead window
*/


/***************************************************************
 * 7. DISABLING READAHEAD
 ***************************************************************

ra_off(ra)

/*
Triggered when:
    - random IO detected
    - excessive cache hits
*/


/***************************************************************
 * 8. CACHE HIT / MISS LOGIC
 ***************************************************************

check_ra_success()

/*
If no IO was needed (page already cached):
    → increase cache_hit counter

If too many hits:
    → disable readahead
    → set RA_FLAG_INCACHE
*/


/*
handle_ra_miss():

    RA_FLAG_MISS
    → next time shrink window
*/


/***************************************************************
 * 9. CORE IO FUNCTION
 ***************************************************************

__do_page_cache_readahead()

/*
Steps:

1. Determine end_index from file size

2. Preallocate pages:
       for each page:
            if not in cache:
                allocate page
                add to page_pool list

3. Submit IO:
       read_pages()
*/


/*
IMPORTANT DESIGN:

    Allocate ALL pages first
    then submit IO

Reason:
    avoid mixing page allocation with writeback
*/


/***************************************************************
 * 10. read_pages()
 ***************************************************************

/*
Two modes:

1. mapping->a_ops->readpages exists:
       batch read

2. fallback:
       call readpage per page
*/


/***************************************************************
 * 11. NON-BLOCKING MODE
 ***************************************************************

blockable_page_cache_readahead()

/*
If queue congested:
    skip IO

Else:
    perform readahead
*/


/*
do_page_cache_readahead():

    if device congested → return -1
*/


/***************************************************************
 * 12. FORCE READAHEAD
 ***************************************************************

force_page_cache_readahead()

/*
Ignores congestion

Splits IO into 2MB chunks

Reason:
    avoid pinning too much memory
*/


/***************************************************************
 * 13. MEMORY-AWARE LIMIT
 ***************************************************************

max_sane_readahead()

/*
Limit readahead based on:

    active + inactive + free pages

Prevents:
    excessive memory pressure
*/


/***************************************************************
 * 14. FULL FLOW (END-TO-END)
 ***************************************************************

/*
read() / page fault
        ↓
page_cache_readahead()
        ↓
__do_page_cache_readahead()
        ↓
allocate pages
        ↓
read_pages()
        ↓
mapping->a_ops->readpage(s)
        ↓
BIO → disk
        ↓
pages become uptodate
        ↓
future reads hit page cache
*/


/***************************************************************
 * 15. RELATION TO WRITEBACK
 ***************************************************************

/*
Read path (this file):
    disk → page cache

Write path (page-writeback.c):
    page cache → disk
*/


/***************************************************************
 * 16. PERFORMANCE INSIGHTS
 ***************************************************************

/*
1. Sequential detection is key
2. Aggressive growth initially
3. Conservative near max
4. Avoid unnecessary IO if data cached
5. Avoid memory pressure via limits
*/


/***************************************************************
 * 17. INTERVIEW MENTAL MODEL
 ***************************************************************

/*
Readahead = prediction system

If access pattern is sequential:
    prefetch future pages

If random:
    disable

Adaptive:
    grow if useful
    shrink if wasteful
*/


/***************************************************************
 * 18. ONE-LINE SUMMARY
 ***************************************************************

/*
readahead.c implements an adaptive, two-window prefetching algorithm
which detects sequential access patterns and preloads file pages into
the page cache to hide disk latency.
*/


/***************************************************************
 * END
 ***************************************************************/

