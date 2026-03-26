/*
 * Linux 2.6.20 — mm/truncate.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (page cache invalidation & truncate)
 *  - What truncate means at VM level
 *  - Partial vs full page truncation
 *  - invalidate vs truncate semantics
 *  - page cache removal flow
 *  - interaction with writeback, dirty state, mapping
 *  - invalidate_inode_pages vs truncate_inode_pages
 *
 * Source: user-provided truncate.c fileciteturn13file0
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
This file is about:

    REMOVING PAGES FROM PAGE CACHE

Specifically when:

    - file is truncated
    - file content becomes invalid
    - pages must be dropped from address_space

This is NOT swap.
This is NOT LRU.

This is:

    "how do we safely destroy page cache state?"
*/


/*
Key concept:

    address_space (mapping)
        ↓
    radix tree of pages

truncate.c is responsible for:

    removing those pages safely
*/


/***************************************************************
 * 1. WHAT IS TRUNCATE?
 ***************************************************************/

/*
Truncate means:

    file size shrinks

Example:

    file size = 8KB
    truncate to 5KB

Then:

    pages covering bytes [5KB, 8KB) must be removed or zeroed
*/


/*
Two cases:

1. Full pages beyond new size → remove page completely
2. Partial page → zero part of page
*/


/***************************************************************
 * 2. PARTIAL PAGE HANDLING
 ***************************************************************/

/*
truncate_partial_page(page, partial)

Flow:

1. zero memory from partial → end of page
2. if PagePrivate → invalidate buffers
*/


/*
Meaning:

Page still exists but content beyond new file size is cleared.
*/


/***************************************************************
 * 3. do_invalidatepage()
 ***************************************************************/

/*
Calls filesystem-specific invalidatepage()

Purpose:

    remove buffer_heads or fs-private metadata
*/


/*
Important:

Must ensure:
    no dirty data exists beyond truncation point
    no I/O ongoing on invalid blocks
*/


/***************************************************************
 * 4. cancel_dirty_page()
 ***************************************************************/

/*
Clears PageDirty flag on page
BUT:
    does NOT clear dirty state in mappings
*/


/*
Used when:
    page is being truncated
*/


/***************************************************************
 * 5. truncate_complete_page()
 ***************************************************************/

/*
Removes entire page from page cache
*/


/*
Flow:

1. verify page->mapping matches
2. cancel_dirty_page()
3. invalidate page buffers
4. ClearPageUptodate
5. ClearPageMappedToDisk
6. remove_from_page_cache()
7. page_cache_release()
*/


/*
Meaning:

    full removal of page from mapping
*/


/***************************************************************
 * 6. invalidate_complete_page()
 ***************************************************************/

/*
Used for invalidate (not truncate)
*/


/*
Difference from truncate:

    does NOT remove dirty pages
    only removes clean unused pages
*/


/*
Flow:

1. if PagePrivate → try_to_release_page()
2. remove_mapping()
*/


/***************************************************************
 * 7. truncate_inode_pages_range() (VERY IMPORTANT)
 ***************************************************************/

/*
Core truncate function
*/


/*
Key idea:

    TWO PASS ALGORITHM

Pass 1:
    non-blocking
    skip locked/writeback pages

Pass 2:
    blocking
    waits and removes remaining pages
*/


/*
Why two passes?

    reduce I/O stalls
    quickly remove easy pages
    defer expensive ones
*/


/***************************************************************
 * 8. PASS 1 FLOW
 ***************************************************************/

/*
for pages in range:

    if locked → skip
    if writeback → skip

    truncate_complete_page()
*/


/***************************************************************
 * 9. PARTIAL PAGE STEP
 ***************************************************************/

/*
If start offset not page aligned:

    find page before start
    wait writeback
    truncate_partial_page()
*/


/***************************************************************
 * 10. PASS 2 FLOW
 ***************************************************************/

/*
for remaining pages:

    lock_page()
    wait writeback
    truncate_complete_page()
*/


/***************************************************************
 * 11. invalidate_mapping_pages()
 ***************************************************************/

/*
Invalidate pages WITHOUT blocking
*/


/*
Conditions:

    skip if:
        dirty
        writeback
        mapped in page tables
*/


/*
Used when:

    want to drop clean cache pages
*/


/***************************************************************
 * 12. invalidate_inode_pages()
 ***************************************************************/

/*
Wrapper:

    invalidate entire mapping
*/


/***************************************************************
 * 13. invalidate_complete_page2()
 ***************************************************************/

/*
Stronger invalidation

Ignores refcount
*/


/*
Used in:

    invalidate_inode_pages2()
*/


/***************************************************************
 * 14. invalidate_inode_pages2_range()
 ***************************************************************/

/*
STRONG INVALIDATION
*/


/*
Key differences:

1. unmaps page from page tables
2. waits for writeback
3. can return -EIO
*/


/*
Flow:

1. lock page
2. unmap_mapping_range()
3. do_launder_page()
4. invalidate_complete_page2()
*/


/***************************************************************
 * 15. unmap_mapping_range()
 ***************************************************************/

/*
Removes PTE mappings of page

Critical for correctness before freeing page
*/


/***************************************************************
 * 16. do_launder_page()
 ***************************************************************/

/*
Calls filesystem launder_page()

Used to flush dirty page before removal
*/


/***************************************************************
 * 17. PAGEVEC USAGE (IMPORTANT)
 ***************************************************************/

/*
pagevec used for batch lookup

Why?
    reduce locking overhead
*/


/***************************************************************
 * 18. FULL FLOW (END-TO-END)
 ***************************************************************/

/*
TRUNCATE:

file size reduced
    ↓
truncate_inode_pages_range()
    ↓
pass 1 remove easy pages
    ↓
handle partial page
    ↓
pass 2 remove remaining pages

INVALIDATE:

invalidate_mapping_pages()
    ↓
remove clean pages only

STRONG INVALIDATE:

invalidate_inode_pages2_range()
    ↓
unmap PTEs
    ↓
remove all pages
*/


/***************************************************************
 * 19. CONNECTION TO OTHER FILES
 ***************************************************************/

/*
Works with:

filemap.c:
    page cache operations

vmscan.c:
    reclaim

swap_state.c:
    swap cache interaction

buffer.c:
    buffer_heads
*/


/***************************************************************
 * 20. KEY INSIGHT (VERY IMPORTANT)
 ***************************************************************/

/*
truncate.c ensures:

    page cache stays consistent with file size

and

    no stale data remains visible or accessible
*/


/***************************************************************
 * 21. INTERVIEW MODEL
 ***************************************************************/

/*
mm/truncate.c handles removal of pages from page cache when file contents
are truncated or invalidated. It carefully manages partial pages, dirty
state, writeback, and page table mappings to ensure correctness.
*/


/***************************************************************
 * 22. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/truncate.c removes and invalidates page cache pages safely during file
truncate and invalidate operations, handling partial pages, dirty state,
and page mappings.
*/


/***************************************************************
 * END
 ***************************************************************/
