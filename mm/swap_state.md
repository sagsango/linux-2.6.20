/*
 * Linux 2.6.20 — mm/swap_state.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - Detailed background and overview
 *  - What the swap cache is
 *  - Why swapper_space exists
 *  - How pages move into/out of swap cache
 *  - read_swap_cache_async() flow
 *  - add_to_swap(), delete_from_swap_cache(), lookup paths
 *  - Relation to page_io.c, rmap.c, and shmem.c
 *
 * Source: Linux 2.6.20 mm/swap_state.c
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
This file manages the SWAP CACHE.

That is the key idea.

When anonymous pages are evicted from RAM, they are written to swap.
But the kernel does not want swap I/O to be treated as a blind disk-only
operation with no cacheable identity.

Instead, it creates a PAGE CACHE-LIKE layer for swap entries.

So conceptually:

    anonymous page in RAM
            ↓
       assigned swap entry
            ↓
    page inserted into swap cache
            ↓
       written to swap device
            ↓
    later fault can find same page via swap entry

This file is the in-memory identity and lookup layer for swapped pages.
*/


/*
You should think of swap_state.c as doing for swap what file page cache
logic does for file-backed pages:

    file page cache:
        (mapping, index) -> page

    swap cache:
        (swap entry) -> page

That is the most important mental model in this file.
*/


/***************************************************************
 * 1. WHY SWAP CACHE EXISTS
 ***************************************************************/

/*
Problem if no swap cache existed:

1. Multiple threads faulting the same swapped-out page could trigger
   duplicate I/O.

2. The kernel would have no simple in-memory place to associate a page
   with a swap entry while I/O is in flight or after writeout.

3. Reclaim / fault / shmem / swapoff paths would be much messier.

So swap cache gives:

    swap entry -> page lookup

with the same general style as page cache lookup.
*/


/*
Benefits:

    - avoid duplicate swap reads
    - allow racing readers to converge on one page
    - allow pages already associated with swap to be found quickly
    - simplify interaction with reclaim and fault paths
    - allow shmem/tmpfs to "swizzle" pages between file cache and swap cache
*/


/***************************************************************
 * 2. swapper_space (VERY IMPORTANT)
 ***************************************************************/

/*
This file defines:

    struct address_space swapper_space

This is described in the source as a "fiction".

Meaning:
    swap is not really a normal filesystem mapping, but the kernel models it
    as an address_space-like object so it can reuse common page-cache/radix-tree
    mechanisms.
*/


/*
Important comment from source:

    swapper_space is a fiction, retained to simplify the path through
    vmscan's shrink_list, to make sync_page look nicer, and to allow
    future use of radix_tree tags in the swap cache.

So swapper_space is a compatibility/unification trick.
*/


/***************************************************************
 * 3. swapper_space CONTENTS
 ***************************************************************/

/*
Key fields:

    .page_tree
        radix tree indexed by swap entry value

    .tree_lock
        protects radix tree

    .a_ops = swap_aops
        operations for swap-backed pages

    .backing_dev_info = swap_backing_dev_info

So the swap cache is literally implemented as a radix tree of pages
living under this synthetic address_space.
*/


/***************************************************************
 * 4. swap_aops
 ***************************************************************/

/*
The address_space_operations used by swapper_space are:

    .writepage      = swap_writepage
    .sync_page      = block_sync_page
    .set_page_dirty = __set_page_dirty_nobuffers
    .migratepage    = migrate_page

Meaning:

swap-backed cached pages can participate in generic page-cache style logic
such as writepage and migration.
*/


/***************************************************************
 * 5. HOW A SWAP-CACHED PAGE IS IDENTIFIED
 ***************************************************************/

/*
For a page in swap cache:

    PageSwapCache(page) == true
    page_private(page)  == entry.val

Unlike normal file page cache pages:
    page->mapping/index are not used the same way here.

Instead:
    swap entry value is stored in page_private()
*/


/*
This is why __add_to_swap_cache() does:

    SetPageSwapCache(page)
    set_page_private(page, entry.val)

That is the identity binding.
*/


/***************************************************************
 * 6. __add_to_swap_cache() (CORE INTERNAL INSERT)
 ***************************************************************/

/*
Function:

    __add_to_swap_cache(page, entry, gfp_mask)

Purpose:

    low-level insertion of page into swapper_space radix tree
*/


/*
Flow:

1. sanity checks:
       page must not already be swapcache
       page must not already have private data

2. radix_tree_preload(gfp_mask)
       preallocate radix-tree nodes if needed

3. lock swapper_space.tree_lock

4. insert page into swapper_space.page_tree using entry.val as index

5. on success:
       page_cache_get(page)
       SetPageLocked(page)
       SetPageSwapCache(page)
       set_page_private(page, entry.val)
       total_swapcache_pages++
       inc NR_FILE_PAGES

6. unlock and finish preload
*/


/*
Important note:

The function increments NR_FILE_PAGES even though this is swap cache.
That is because swap cache pages are accounted alongside page-cache-like
page state, not as anonymous RSS at this point.
*/


/***************************************************************
 * 7. add_to_swap_cache() (PUBLICER WRAPPER)
 ***************************************************************/

/*
Function:

    add_to_swap_cache(page, entry)

Purpose:

    insert page into swap cache while also taking a swap reference via
    swap_duplicate(entry)
*/


/*
Why swap_duplicate() first?

Because a swap entry must remain valid while the page is cached against it.
The swap system maintains reference counts/usages for swap entries.

Flow:

1. swap_duplicate(entry)
       if fails -> entry no longer valid (-ENOENT)

2. call __add_to_swap_cache()

3. if insertion fails:
       swap_free(entry)
       account race stats

4. on success:
       record success stats
*/


/***************************************************************
 * 8. add_to_swap() (ALLOCATE SWAP FOR PAGE)
 ***************************************************************/

/*
Function:

    add_to_swap(page, gfp_mask)

Purpose:

    allocate a fresh swap entry for a page and put that page into swap cache

This is a central step in swap-out.
*/


/*
Conceptual meaning:

    page in RAM
        ↓
    reserve slot on swap device
        ↓
    insert page into swap cache under that slot
        ↓
    page is now logically backed by swap
*/


/*
Flow:

1. caller must hold page lock
2. loop:
       entry = get_swap_page()
       if none -> fail

3. call __add_to_swap_cache(page, entry, gfp_mask | __GFP_NOMEMALLOC | __GFP_NOWARN)

4. outcomes:

   success:
       SetPageUptodate(page)
       SetPageDirty(page)
       stats++
       return 1

   -EEXIST:
       someone raced (typically speculative read_swap_cache_async)
       free the just-allocated swap entry and retry

   other failure (usually -ENOMEM in radix tree allocation):
       free swap entry
       return 0
*/


/*
Important subtlety:

This function allocates swap entry first and then binds page to it in cache.
If insertion races with an existing cached page for same entry, the code treats
that as a race and retries.
*/


/***************************************************************
 * 9. delete_from_swap_cache() / __delete_from_swap_cache()
 ***************************************************************/

/*
Purpose:

    remove a page from swap cache and drop the swap entry reference
*/


/*
Internal helper:

    __delete_from_swap_cache(page)

expects:
    - page locked
    - page is in swap cache
    - page not under writeback
*/


/*
Flow of __delete_from_swap_cache():

1. radix_tree_delete(swapper_space.page_tree, page_private(page))
2. set_page_private(page, 0)
3. ClearPageSwapCache(page)
4. total_swapcache_pages--
5. dec NR_FILE_PAGES
6. stats++
*/


/*
Public wrapper:

    delete_from_swap_cache(page)

Flow:

1. entry = page_private(page)
2. lock swapper_space.tree_lock
3. __delete_from_swap_cache(page)
4. unlock
5. swap_free(entry)
6. page_cache_release(page)

Meaning:
    remove page<->swapentry association and drop references on both sides.
*/


/***************************************************************
 * 10. lookup_swap_cache()
 ***************************************************************/

/*
Function:

    lookup_swap_cache(entry)

Purpose:

    look up an already-cached page by swap entry
*/


/*
Implementation:

    find_get_page(&swapper_space, entry.val)

So again, swap cache behaves like page cache lookup under swapper_space.
*/


/*
Return rules:

    returns unlocked page with refcount incremented

This is a crucial property for fault paths and races.
*/


/***************************************************************
 * 11. read_swap_cache_async() (MOST IMPORTANT READ PATH)
 ***************************************************************/

/*
Function:

    read_swap_cache_async(entry, vma, addr)

This is one of the most important functions in the file.

Purpose:

    locate a swap entry in memory if already cached;
    otherwise allocate a page, insert it into swap cache, and start I/O.
*/


/*
This function is the bridge between:

    page fault on swapped-out anonymous page
and
    swap device read I/O
*/


/*
Detailed flow:

LOOP:

1. first check if page is already in swap cache:
       found_page = find_get_page(&swapper_space, entry.val)
       if found -> return it

2. if not found, allocate a new page if we don't already have one:
       new_page = alloc_page_vma(GFP_HIGHUSER, vma, addr)

3. try to add new_page to swap cache with add_to_swap_cache(new_page, entry)

4. outcomes:

   success:
       lru_cache_add_active(new_page)
       swap_readpage(NULL, new_page)
       return new_page

   -EEXIST:
       another thread inserted same entry concurrently
       loop again and find existing page

   -ENOENT:
       swap entry no longer valid
       give up

   -ENOMEM:
       unable to allocate support structures / page
       give up

5. on failure release new_page and return found_page or NULL
*/


/*
Important race insight:

Two threads can fault the same swap entry at once.

Both might allocate new pages.
But only one can win insertion into swap cache.
The loser sees -EEXIST, drops its speculative page, and retries lookup.

This avoids duplicate long-term cache entries and converges the race onto
one page.
*/


/***************************************************************
 * 12. WHY read_swap_cache_async() IS CALLED "ASYNC"
 ***************************************************************/

/*
Because once the page is inserted into swap cache, the function starts
swap_readpage() and returns the locked page while I/O may still be in flight.

Callers can then wait on the page lock / uptodate state as needed.

So:

    allocation + cache binding + I/O start
        happen here

    completion waiting
        often happens elsewhere
*/


/***************************************************************
 * 13. move_to_swap_cache() (SPECIAL SHMEM PATH)
 ***************************************************************/

/*
Function:

    move_to_swap_cache(page, entry)

Comment in source:
    "Strange swizzling function only for use by shmem_writepage"

This is a very important special-case helper for tmpfs/shmem.
*/


/*
Meaning:

Unlike ordinary anon swapout, shmem pages may currently live in a normal
page cache mapping (tmpfs inode mapping). To swap them out, kernel wants to:

    page cache page  ->  swap cache page

without creating a second duplicate page object.
*/


/*
Flow:

1. __add_to_swap_cache(page, entry, GFP_ATOMIC)
2. if success:
       remove_from_page_cache(page)
       page_cache_release(page)   // drop file pagecache ref
       swap_duplicate(entry)      // maintain swap ref
       SetPageDirty(page)
       stats++

So same page is "swizzled" from file-page-cache identity to swap-cache identity.
*/


/***************************************************************
 * 14. move_from_swap_cache() (SPECIAL SHMEM REVERSE PATH)
 ***************************************************************/

/*
Function:

    move_from_swap_cache(page, index, mapping)

Comment in source:
    "Strange swizzling function for shmem_getpage (and shmem_unuse)"

This is the reverse of move_to_swap_cache().
*/


/*
Meaning:

When a shmem/tmpfs page is brought back from swap, kernel wants to move the
same page object from swap cache back into the file mapping's page cache.
*/


/*
Flow:

1. add_to_page_cache(page, mapping, index, GFP_ATOMIC)
2. if success:
       delete_from_swap_cache(page)
       ClearPageDirty(page)
       set_page_dirty(page)

That final dirty handling is used to shift accounting/list behavior from the
swap cache side back to file page cache side.
*/


/***************************************************************
 * 15. free_swap_cache() / free_page_and_swap_cache()
 ***************************************************************/

/*
Purpose:

    opportunistically free swap cache association when page is exclusively used
*/


/*
free_swap_cache(page):

1. if PageSwapCache(page)
2. try lock page
3. call remove_exclusive_swap_page(page)
4. unlock

Meaning:
    if page is swap-cached and no longer needs that backing association,
    try to remove it.
*/


/*
free_page_and_swap_cache(page):

1. free_swap_cache(page)
2. page_cache_release(page)

So release page, but first try to drop swap-cache state if possible.
*/


/***************************************************************
 * 16. free_pages_and_swap_cache()
 ***************************************************************/

/*
Purpose:

    batch version for arrays of pages

Flow:

1. lru_add_drain()
2. for chunks up to PAGEVEC_SIZE:
       free_swap_cache(each)
       release_pages(chunk)

Used to efficiently release multiple pages while also stripping swap cache
associations where possible.
*/


/***************************************************************
 * 17. SWAP CACHE STATISTICS
 ***************************************************************/

/*
The file keeps stats in swap_cache_info:

    add_total
    del_total
    find_success
    find_total
    noent_race
    exist_race

These are diagnostic counters for understanding swap-cache behavior and races.
*/


/*
show_swap_cache_info() prints:
    - add/delete counts
    - lookup hit rate
    - race counts
    - free swap size
    - total swap size
*/


/***************************************************************
 * 18. IMPORTANT RACES HANDLED HERE
 ***************************************************************/

/*
Race 1: swap entry disappears before add
---------------------------------------
add_to_swap_cache() may see swap_duplicate(entry) fail.
Meaning the entry is gone; returns -ENOENT.

Race 2: two readers race to instantiate same swapped page
--------------------------------------------------------
read_swap_cache_async() may see -EEXIST when another thread inserted first.
Loser retries lookup.

Race 3: swap entry reused quickly
---------------------------------
Comments mention race with try_to_swap_out or shmem_writepage reusing a just
freed swap entry for an existing page.
Again, -EEXIST handling covers this case.
*/


/***************************************************************
 * 19. RELATION TO page_io.c
 ***************************************************************/

/*
page_io.c does the actual low-level swap I/O submission logic.

swap_state.c does the in-memory cache/identity/lookup logic.

So:

    swap_state.c
        = page exists in swap cache? how do we associate page<->entry?

    page_io.c
        = actually read/write page to swap device
*/


/*
When read_swap_cache_async() wins insertion and calls:

    swap_readpage(NULL, new_page)

that transitions into the lower-level swap I/O machinery.
*/


/***************************************************************
 * 20. RELATION TO rmap.c / try_to_unmap()
 ***************************************************************/

/*
rmap.c and reclaim paths use swap entries in PTEs when anonymous pages are
unmapped during reclaim.

swap_state.c is what makes it possible for those swap entries to correspond
cleanly to actual in-memory page objects when needed.

So conceptually:

    reclaim decides page should move out
        ↓
    swap entry gets assigned
        ↓
    page enters swap cache (swap_state.c)
        ↓
    actual write happens (page_io.c)
        ↓
    later fault uses swap entry to find/read page (swap_state.c again)
*/


/***************************************************************
 * 21. RELATION TO shmem.c
 ***************************************************************/

/*
shmem/tmpfs heavily uses the special swizzling helpers:

    move_to_swap_cache()
    move_from_swap_cache()

Because tmpfs pages can live either in file page cache (inode mapping) or in
swap cache, and the kernel often wants to move the same struct page between
those identities instead of allocating a second page.

This is one of the reasons this file matters beyond plain anonymous swapping.
*/


/***************************************************************
 * 22. FULL END-TO-END STORY (VERY IMPORTANT)
 ***************************************************************/

/*
ANONYMOUS PAGE SWAP-OUT STORY:

anonymous page selected for reclaim
    ↓
assign swap entry (add_to_swap / related path)
    ↓
insert page into swap cache
    ↓
write page to swap device
    ↓
PTEs point to swap entry, page may later leave RAM

ANONYMOUS PAGE SWAP-IN STORY:

fault on swap entry in PTE
    ↓
lookup_swap_cache(entry)
    ↓
if miss:
    read_swap_cache_async(entry, vma, addr)
        ↓
    allocate page
    insert into swap cache
    start swap_readpage()
        ↓
wait for page I/O completion elsewhere
        ↓
map page back into process
*/


/***************************************************************
 * 23. THE SINGLE BEST MENTAL MODEL
 ***************************************************************/

/*
If file page cache is:

    (mapping, index) -> page

then swap cache is:

    (swap entry value) -> page

That single analogy explains most of swap_state.c.
*/


/***************************************************************
 * 24. INTERVIEW MODEL
 ***************************************************************/

/*
If asked:

"What does mm/swap_state.c do?"

Good answer:

    It implements the swap cache, a page-cache-like radix-tree mapping from
    swap entries to struct page objects. This lets the kernel avoid duplicate
    swap reads, coordinate races in swap-in/swap-out, and move pages between
    ordinary page cache and swap cache (especially for shmem/tmpfs). It is
    the in-memory identity and lookup layer for swapped pages, while lower
    layers like page_io.c perform the actual I/O.
*/


/***************************************************************
 * 25. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/swap_state.c implements the swap cache: a radix-tree-backed mapping from
swap entries to struct page objects that supports swap-in, swap-out, race
handling, and special page swizzling for shmem/tmpfs.
*/


/***************************************************************
 * END
 ***************************************************************/

