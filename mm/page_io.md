/*
 * FILE: linux/mm/page_io.c  (Linux 2.6.20)
 *
 * TITLE:
 *   VERY DETAILED IDE NOTES / STUDY FILE
 *
 * TOPIC:
 *   SWAP I/O PATH
 *   How Linux actually reads pages from swap and writes pages to swap
 *
 * GOAL:
 *   Build strong low-level understanding of:
 *     - what page_io.c does
 *     - how swap writeback is submitted
 *     - how swap-in I/O completes
 *     - relationship with swap cache, BIO, page flags, and page lock
 *     - where this file sits in the larger VM path
 */

/*****************************************************************************/
/* 0. FIRST BIG SUMMARY                                                       */
/*****************************************************************************/

/*
 * page_io.c is the LOW-LEVEL SWAP BLOCK I/O glue.
 *
 * It is the place where the MM layer finally says:
 *
 *   "I have a page that belongs to swap entry X.
 *    Please issue actual block I/O to the swap device."
 *
 * So this file does NOT decide:
 *   - which page to reclaim
 *   - when to swap
 *   - which victim task to kill
 *   - how swap cache lookup works in full detail
 *
 * Instead, this file does the narrow but critical job:
 *
 *   1. Build a BIO for one page worth of swap I/O
 *   2. Map swap entry -> disk sector
 *   3. Submit READ or WRITE BIO
 *   4. On completion, update page state
 *      (PageUptodate / PageError / unlock_page / end_page_writeback)
 *
 * So think of it like this:
 *
 *   higher VM code decides "swap this page"
 *                     |
 *                     v
 *              page_io.c does the actual I/O
 *                     |
 *                     v
 *             block layer + swap device
 */

/*****************************************************************************/
/* 1. WHERE THIS FILE FITS IN THE BIGGER VM FLOW                              */
/*****************************************************************************/

/*
 * A) SWAP-OUT PATH (page written from RAM to swap)
 *
 *   reclaim logic
 *      -> isolates page
 *      -> decides anonymous/swap-backed page must go out
 *      -> writepage path for swap cache page
 *      -> swap_writepage(page, wbc)
 *      -> get_swap_bio(...)
 *      -> submit_bio(WRITE, bio)
 *      -> device writes PAGE_SIZE bytes to swap slot
 *      -> end_swap_bio_write()
 *      -> end_page_writeback(page)
 *
 * B) SWAP-IN PATH (page fault causes page to be read from swap)
 *
 *   page fault
 *      -> do_swap_page()
 *      -> read_swap_cache_async(...)
 *      -> swap_readpage(file, page)
 *      -> get_swap_bio(...)
 *      -> submit_bio(READ, bio)
 *      -> device reads PAGE_SIZE bytes from swap slot
 *      -> end_swap_bio_read()
 *      -> SetPageUptodate(page) / SetPageError(page)
 *      -> unlock_page(page)
 *      -> upper layer continues fault handling
 */

/*****************************************************************************/
/* 2. WHAT THIS FILE REALLY OPERATES ON                                       */
/*****************************************************************************/

/*
 * Important objects:
 *
 *   struct page *page
 *     - physical page frame in RAM
 *     - contents either need to be written to swap, or filled from swap
 *
 *   page_private(page)
 *     - stores swap entry encoding / swap slot index information
 *     - here used to identify WHICH swap slot this page belongs to
 *
 *   swp_entry_t
 *     - logical swap entry
 *     - contains swap type + swap offset
 *
 *   struct swap_info_struct *sis
 *     - describes a swap area/device
 *     - contains device info, mapping details, etc.
 *
 *   struct bio
 *     - block I/O request descriptor
 *     - here always one page long
 */

/*****************************************************************************/
/* 3. HIGH-LEVEL RESPONSIBILITIES OF EACH FUNCTION                            */
/*****************************************************************************/

/*
 * get_swap_bio()
 *   -> allocate and initialize one BIO for one swap page
 *
 * end_swap_bio_write()
 *   -> completion handler for swap WRITE
 *
 * end_swap_bio_read()
 *   -> completion handler for swap READ
 *
 * swap_writepage()
 *   -> called when writing a swap-cache page out to disk
 *
 * swap_readpage()
 *   -> called when reading a swap-cache page in from disk
 */

/*****************************************************************************/
/* 4. get_swap_bio() — CORE BIO CONSTRUCTION                                  */
/*****************************************************************************/

static struct bio *get_swap_bio(gfp_t gfp_flags, pgoff_t index,
                                struct page *page, bio_end_io_t end_io)

/*
 * This function prepares a BIO that represents one page of swap I/O.
 *
 * Inputs:
 *   gfp_flags : allocation flags for bio_alloc()
 *   index     : swap entry encoding stored as page index-like value
 *   page      : RAM page involved in the I/O
 *   end_io    : completion callback
 *
 * Internal flow:
 *
 *   bio = bio_alloc(gfp_flags, 1)
 *
 * Meaning:
 *   allocate BIO with room for exactly one bio_vec entry.
 *   This file always submits one PAGE_SIZE chunk at a time.
 */

/*
 * Then:
 *
 *   swp_entry_t entry = { .val = index };
 *
 * Meaning:
 *   reinterpret the supplied index as a swap entry.
 *   This gives us access to:
 *     swp_type(entry)   -> which swap area/device
 *     swp_offset(entry) -> slot number inside that area
 */

/*
 * Then:
 *
 *   sis = get_swap_info_struct(swp_type(entry));
 *
 * Meaning:
 *   locate swap metadata for the corresponding swap area.
 */

/*
 * Then sector mapping:
 *
 *   bio->bi_sector = map_swap_page(sis, swp_offset(entry)) * (PAGE_SIZE >> 9)
 *
 * This is VERY IMPORTANT.
 *
 * map_swap_page(...) gives page-sized slot mapping on the swap device.
 *
 * (PAGE_SIZE >> 9) converts pages into 512-byte sectors, because block
 * layer sector numbering is traditionally in 512-byte units.
 *
 * Example:
 *   PAGE_SIZE = 4096
 *   PAGE_SIZE >> 9 = 8 sectors
 *
 * So if swap slot N maps to page slot S on disk, BIO starts at sector S*8.
 */

/*
 * Then BIO is filled:
 *
 *   bio->bi_bdev = sis->bdev
 *
 * means target block device = swap device
 *
 *   bio->bi_io_vec[0].bv_page = page
 *   bio->bi_io_vec[0].bv_len = PAGE_SIZE
 *   bio->bi_io_vec[0].bv_offset = 0
 *
 * means this BIO transfers full PAGE_SIZE from offset 0 inside page
 *
 *   bio->bi_vcnt = 1
 *   bio->bi_idx = 0
 *   bio->bi_size = PAGE_SIZE
 *   bio->bi_end_io = end_io
 *
 * So in summary, get_swap_bio() builds:
 *
 *   "Transfer exactly one page between this RAM page and this swap-device
 *    sector range, and call this completion handler when done."
 */

/*****************************************************************************/
/* 5. WRITE COMPLETION PATH                                                   */
/*****************************************************************************/

static int end_swap_bio_write(struct bio *bio, unsigned int bytes_done, int err)

/*
 * This runs when a WRITE bio completes.
 *
 * Key first check:
 *
 *   if (bio->bi_size)
 *       return 1;
 *
 * Historical BIO completion convention:
 *   BIO may complete partially first.
 *   Only when bi_size reaches 0 is whole request finished.
 *
 * So:
 *   bi_size != 0 -> not fully done yet
 *   bi_size == 0 -> complete request finalization now
 */

/*
 * It tests:
 *
 *   uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags)
 *
 * Meaning:
 *   did lower layer say this I/O succeeded?
 */

/*
 * On write failure:
 *
 *   SetPageError(page)
 *   set_page_dirty(page)
 *   printk(...)
 *   ClearPageReclaim(page)
 *
 * Detailed meaning:
 *
 *   SetPageError(page)
 *     - remember I/O error on page
 *
 *   set_page_dirty(page)
 *     - VERY important
 *     - if swap write failed, page data is NOT safely stored in swap
 *     - page must remain dirty so reclaim doesn't think it has a clean
 *       persistent copy somewhere
 *
 *   printk(...)
 *     - logs swap device write error
 *
 *   ClearPageReclaim(page)
 *     - avoids reclaim rotation logic treating page as successfully reclaimable
 */

/*
 * Whether success or failure, it does:
 *
 *   end_page_writeback(page)
 *
 * This is crucial.
 *
 * Earlier, writeback was started with set_page_writeback(page).
 * That marks page as under writeback.
 *
 * end_page_writeback(page) says:
 *   "the writeback lifecycle for this page is finished now"
 *
 * Then:
 *   bio_put(bio)
 *
 * free BIO reference
 */

/*****************************************************************************/
/* 6. READ COMPLETION PATH                                                    */
/*****************************************************************************/

int end_swap_bio_read(struct bio *bio, unsigned int bytes_done, int err)

/*
 * Again, bi_size check means finalize only when entire BIO is complete.
 */

/*
 * On read failure:
 *
 *   SetPageError(page)
 *   ClearPageUptodate(page)
 *   printk(...)
 *
 * Meaning:
 *   the page contents in RAM are not valid
 *   fault path must see that this page is not uptodate
 */

/*
 * On success:
 *
 *   SetPageUptodate(page)
 *
 * Meaning:
 *   page now contains correct contents read from swap
 */

/*
 * Finally:
 *
 *   unlock_page(page)
 *   bio_put(bio)
 *
 * This is the signal to any waiter:
 *   "swap-in I/O is over; page state is now valid to inspect"
 *
 * This is exactly what upper layers wait for after issuing readpage.
 */

/*****************************************************************************/
/* 7. swap_writepage() — WRITING A PAGE TO SWAP                               */
/*****************************************************************************/

int swap_writepage(struct page *page, struct writeback_control *wbc)

/*
 * This is the writepage method for swap cache pages.
 *
 * Called when VM wants to push page contents to swap device.
 */

/*
 * First interesting optimization:
 *
 *   if (remove_exclusive_swap_page(page)) {
 *       unlock_page(page);
 *       goto out;
 *   }
 *
 * Meaning:
 *   if this swap cache page is exclusive / unnecessary / stale, then maybe
 *   we do not need to actually write it.
 *
 * The source comment says:
 *
 *   stale swap cache pages may exist in memory
 *   avoid unnecessary final write
 *
 * So this is an optimization to skip useless disk traffic.
 */

/*
 * Then BIO creation:
 *
 *   bio = get_swap_bio(GFP_NOIO, page_private(page), page, end_swap_bio_write)
 *
 * Important details:
 *
 *   GFP_NOIO
 *     - do not recurse into more I/O while we are already in reclaim/writeback
 *     - standard defensive allocation flag in low-level memory writeout paths
 *
 *   page_private(page)
 *     - swap entry index for this page
 *
 *   end_swap_bio_write
 *     - completion handler for write
 */

/*
 * If BIO allocation fails:
 *
 *   set_page_dirty(page)
 *   unlock_page(page)
 *   ret = -ENOMEM
 *
 * Why dirty again?
 *   because write submission never happened; page still needs writeback later
 */

/*
 * Sync mode handling:
 *
 *   if (wbc->sync_mode == WB_SYNC_ALL)
 *       rw |= (1 << BIO_RW_SYNC)
 *
 * Meaning:
 *   for stronger synchronous writeback requests, tell block layer this is sync
 */

/*
 * Accounting:
 *
 *   count_vm_event(PSWPOUT)
 *
 * increments swap-out statistics
 */

/*
 * Before submit:
 *
 *   set_page_writeback(page)
 *   unlock_page(page)
 *   submit_bio(rw, bio)
 *
 * This ordering matters.
 *
 * set_page_writeback(page)
 *   marks page under writeback
 *
 * unlock_page(page)
 *   page lock is released before I/O completes
 *   other code can see page is under writeback and wait appropriately
 *
 * submit_bio(...)
 *   sends request to block layer/device
 */

/*
 * So swap_writepage() lifecycle is:
 *
 *   locked page in
 *     -> maybe skip stale write
 *     -> build BIO
 *     -> mark writeback
 *     -> unlock page
 *     -> submit async disk write
 *     -> later completion handler ends writeback
 */

/*****************************************************************************/
/* 8. swap_readpage() — READING A PAGE FROM SWAP                              */
/*****************************************************************************/

int swap_readpage(struct file *file, struct page *page)

/*
 * Called when a page must be read in from swap.
 * The page is locked on entry.
 */

/*
 * First:
 *
 *   BUG_ON(!PageLocked(page));
 *
 * means caller must provide locked page.
 */

/*
 * Then:
 *
 *   ClearPageUptodate(page)
 *
 * We are about to issue I/O to fill this page.
 * Until successful completion, page contents must not be treated as valid.
 */

/*
 * Build BIO:
 *
 *   bio = get_swap_bio(GFP_KERNEL, page_private(page), page, end_swap_bio_read)
 *
 * Why GFP_KERNEL here, not GFP_NOIO?
 *   read path is initiated from fault-side / swap cache fill context,
 *   and this older code uses GFP_KERNEL for the bio allocation.
 */

/*
 * If BIO allocation fails:
 *
 *   unlock_page(page)
 *   ret = -ENOMEM
 *
 * Since no I/O was started, page remains not uptodate and unlocked.
 * Upper layers will handle the failure.
 */

/*
 * On success:
 *
 *   count_vm_event(PSWPIN)
 *   submit_bio(READ, bio)
 *
 * Then later end_swap_bio_read() will:
 *   - set uptodate on success or error on failure
 *   - unlock the page
 */

/*****************************************************************************/
/* 9. IMPORTANT PAGE FLAG TRANSITIONS                                          */
/*****************************************************************************/

/*
 * WRITE path:
 *
 *   before submit:
 *       page locked
 *       maybe dirty
 *
 *   set_page_writeback(page)
 *   unlock_page(page)
 *
 *   later completion:
 *       success/failure recorded
 *       end_page_writeback(page)
 *
 * READ path:
 *
 *   before submit:
 *       page locked
 *       PageUptodate cleared
 *
 *   submit_bio(READ)
 *
 *   later completion:
 *       success -> SetPageUptodate(page)
 *       failure -> SetPageError(page), ClearPageUptodate(page)
 *       unlock_page(page)
 */

/*****************************************************************************/
/* 10. WHY READ COMPLETION UNLOCKS PAGE, BUT WRITE COMPLETION ENDS WRITEBACK  */
/*****************************************************************************/

/*
 * This is a subtle but very important conceptual point.
 *
 * SWAP READ:
 *   caller locked the page because it needs page contents before continuing
 *   once I/O completes, unlocking page tells waiters:
 *       "page fill is done"
 *
 * SWAP WRITE:
 *   writeback path typically unlocks page BEFORE I/O completes,
 *   but keeps PageWriteback set
 *   completion then calls end_page_writeback(page)
 *
 * So:
 *   READ completion finishes by unlock_page()
 *   WRITE completion finishes by end_page_writeback()
 */

/*****************************************************************************/
/* 11. RELATIONSHIP WITH SWAP CACHE                                           */
/*****************************************************************************/

/*
 * This file assumes the page already corresponds to some swap entry.
 *
 * It does not allocate swap slot here.
 * It does not insert page into swap cache here.
 * It does not decide page replacement here.
 *
 * That happened earlier.
 *
 * page_io.c only uses:
 *
 *   page_private(page)
 *
 * as the already-established handle telling it where to do disk I/O.
 */

/*****************************************************************************/
/* 12. RELATIONSHIP WITH memory.c / do_swap_page()                            */
/*****************************************************************************/

/*
 * Typical swap-in chain:
 *
 *   page fault on swapped-out page
 *      -> do_swap_page()
 *      -> lookup_swap_cache(entry)
 *      -> if not present:
 *            read_swap_cache_async(entry, vma, addr)
 *               -> alloc/find page in swap cache
 *               -> swap_readpage(...)
 *      -> wait for page lock / uptodate
 *      -> install pte
 *
 * So page_io.c is the low-level I/O worker used by that higher logic.
 */

/*****************************************************************************/
/* 13. RELATIONSHIP WITH RECLAIM / WRITEBACK                                  */
/*****************************************************************************/

/*
 * Typical swap-out chain:
 *
 *   reclaim selects page
 *      -> swap cache / swap slot already associated
 *      -> writeback path calls swap_writepage()
 *      -> page_io.c submits disk write
 *      -> on success, page may later become reclaimable/freeable
 *
 * This file itself does not free the page.
 * It only ensures persistent copy reaches swap device.
 */

/*****************************************************************************/
/* 14. WHY BIO SIZE IS EXACTLY PAGE_SIZE                                      */
/*****************************************************************************/

/*
 * Swap I/O is page-granular here.
 *
 * Every swap slot corresponds to one page.
 * Every BIO issued by this file transfers exactly one page.
 *
 * That keeps the logic very simple:
 *
 *   one page <-> one swap entry <-> one BIO <-> one completion callback
 */

/*****************************************************************************/
/* 15. ERROR HANDLING PHILOSOPHY                                              */
/*****************************************************************************/

/*
 * WRITE failure:
 *   - page marked error
 *   - page redirtied
 *   - reclaim prevented from incorrectly discarding it
 *
 * READ failure:
 *   - page marked error
 *   - page not uptodate
 *   - fault-side logic will see failure later
 *
 * BIO allocation failure:
 *   - no I/O issued
 *   - page unlocked
 *   - return -ENOMEM
 */

/*****************************************************************************/
/* 16. IMPORTANT BACKGROUND CONCEPTS                                          */
/*****************************************************************************/

/*
 * (A) PAGE LOCK
 *
 *   Means page state/content is under controlled transition.
 *
 *   For swap_readpage:
 *     page is locked while being filled.
 *
 *   For swap_writepage:
 *     page starts locked, but writeback model unlocks it before I/O ends.
 */

/*
 * (B) PAGE_WRITEBACK
 *
 *   Means write I/O is in flight for this page.
 *   Waiters use wait_on_page_writeback(page).
 */

/*
 * (C) PAGE_UPTODATE
 *
 *   Means page contents are valid and complete.
 *
 *   swap read success -> set it
 *   swap read failure -> clear it
 */

/*
 * (D) PAGE_ERROR
 *
 *   Sticky indication that I/O problem happened.
 */

/*****************************************************************************/
/* 17. SMALL END-TO-END WRITE EXAMPLE                                         */
/*****************************************************************************/

/*
 * Suppose anonymous page P is in swap cache, with swap entry E.
 * Reclaim wants to evict it.
 *
 *   swap_writepage(P, wbc)
 *     -> remove_exclusive_swap_page(P)? maybe no
 *     -> get_swap_bio(..., E, P, end_swap_bio_write)
 *     -> count_vm_event(PSWPOUT)
 *     -> set_page_writeback(P)
 *     -> unlock_page(P)
 *     -> submit_bio(WRITE, bio)
 *
 * Device completes later:
 *
 *   end_swap_bio_write()
 *     -> if success: just end_page_writeback(P)
 *     -> if fail: mark error, dirty again, clear reclaim, end writeback
 */

/*****************************************************************************/
/* 18. SMALL END-TO-END READ EXAMPLE                                          */
/*****************************************************************************/

/*
 * Process faults on virtual address whose PTE is swap entry E.
 * Higher layer allocates page P in swap cache and locks it.
 *
 *   swap_readpage(file, P)
 *     -> ClearPageUptodate(P)
 *     -> get_swap_bio(..., E, P, end_swap_bio_read)
 *     -> count_vm_event(PSWPIN)
 *     -> submit_bio(READ, bio)
 *
 * Device completes later:
 *
 *   end_swap_bio_read()
 *     -> success: SetPageUptodate(P)
 *     -> failure: SetPageError(P), ClearPageUptodate(P)
 *     -> unlock_page(P)
 *
 * Higher fault path then continues based on page state.
 */

/*****************************************************************************/
/* 19. WHAT THIS FILE DOES NOT DO                                             */
/*****************************************************************************/

/*
 * page_io.c does NOT:
 *
 *   - choose victim pages
 *   - maintain LRU
 *   - decide swap slot allocation policy
 *   - install swap PTEs
 *   - perform full swap cache lookup policy
 *   - free RAM page after write
 *   - resolve page fault completely
 *
 * It is purely low-level transport between RAM page and swap block device.
 */

/*****************************************************************************/
/* 20. FINAL MENTAL MODEL                                                     */
/*****************************************************************************/

/*
 * Best mental model:
 *
 *   memory.c / reclaim code says:
 *       "I know WHICH swap entry this page belongs to"
 *
 *   page_io.c says:
 *       "Okay, I will convert that swap entry into BIO + sectors,
 *        submit block I/O, and update page flags on completion"
 *
 * That is the whole point of this file.
 */

/*****************************************************************************/
/* 21. ULTRA-CONDENSED CHEAT SHEET                                            */
/*****************************************************************************/

/*
 * get_swap_bio()
 *   swap entry -> bio + device + sector + page
 *
 * swap_writepage()
 *   write RAM page -> swap device
 *   set writeback, unlock page, submit BIO
 *
 * end_swap_bio_write()
 *   if fail: dirty again
 *   end_page_writeback(page)
 *
 * swap_readpage()
 *   read swap device -> RAM page
 *   clear uptodate, submit BIO
 *
 * end_swap_bio_read()
 *   success -> SetPageUptodate
 *   failure -> SetPageError
 *   unlock_page(page)
 */

/*****************************************************************************/
/* END                                                                        */
/*****************************************************************************/

