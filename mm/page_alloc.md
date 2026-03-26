/*
 * FILE: linux/mm/page_alloc.c  (Linux 2.6.20)
 *
 * TITLE:
 *   VERY DETAILED IDE NOTES / STUDY FILE
 *
 * BASED ON SOURCE:
 *   page allocator + buddy system + zonelist allocation + reclaim + OOM trigger
 *
 * SOURCE REF:
 *   fileciteturn14file0
 *
 * GOAL:
 *   Deep understanding of:
 *     - buddy allocator
 *     - zonelists
 *     - allocation fast path vs slow path
 *     - per-cpu pages
 *     - watermarks
 *     - reclaim + OOM interaction
 */

/*****************************************************************************/
/* 0. VERY HIGH LEVEL SUMMARY                                                 */
/*****************************************************************************/

/*
 * page_alloc.c is THE HEART of physical memory allocation in Linux.
 *
 * Everything eventually comes here:
 *
 *   kmalloc() -> slab -> alloc_pages()
 *   page cache -> alloc_pages()
 *   user memory faults -> alloc_pages()
 *   kernel stacks, buffers, etc
 *
 * Core responsibilities:
 *
 *   1. Maintain free memory (buddy system)
 *   2. Allocate pages (__alloc_pages)
 *   3. Free pages (__free_pages)
 *   4. Manage zones + NUMA fallback (zonelists)
 *   5. Interact with reclaim + OOM
 */

/*****************************************************************************/
/* 1. BIG PICTURE FLOW                                                        */
/*****************************************************************************/

/*
 *                alloc_pages(gfp, order)
 *                        |
 *                        v
 *                 __alloc_pages()
 *                        |
 *        +---------------+----------------+
 *        |                                |
 *        v                                v
 *  fast path (freelist)           slow path (reclaim)
 *        |                                |
 *        v                                v
 * buffered_rmqueue()             try_to_free_pages()
 *        |                                |
 *        v                                v
 * buddy allocator                OOM (if fail)
 */

/*****************************************************************************/
/* 2. CORE DATA STRUCTURES                                                    */
/*****************************************************************************/

/*
 * struct zone
 *   - represents memory region (DMA, NORMAL, HIGHMEM)
 *   - contains free_area[MAX_ORDER]
 *   - maintains free_pages, watermarks
 *
 * struct free_area
 *   - free_list: linked list of free blocks
 *   - nr_free: number of free blocks of that order
 *
 * struct page
 *   - represents physical page frame
 *   - flags: PG_buddy, PG_lru etc
 *   - private: used for order in buddy system
 */

/*****************************************************************************/
/* 3. BUDDY SYSTEM CORE IDEA                                                  */
/*****************************************************************************/

/*
 * Memory is divided into blocks of size:
 *
 *   2^order pages
 *
 * Example:
 *   order 0 -> 1 page
 *   order 1 -> 2 pages
 *   order 2 -> 4 pages
 *   ...
 *
 * Key operations:
 *
 *   Allocation:
 *     find smallest available >= order
 *     split if needed
 *
 *   Free:
 *     check buddy
 *     merge if buddy free
 */

/*****************************************************************************/
/* 4. BUDDY FINDING                                                           */
/*****************************************************************************/

/*
 * buddy_index = page_idx ^ (1 << order)
 *
 * Meaning:
 *   flip bit corresponding to block size
 *
 * combine_index = page_idx & ~(1 << order)
 *
 * Meaning:
 *   parent block index
 */

/*****************************************************************************/
/* 5. FREE PATH                                                               */
/*****************************************************************************/

/*
 * __free_pages() -> __free_pages_ok()
 *
 * __free_pages_ok():
 *   - validate page
 *   - arch_free_page()
 *   - call free_one_page()
 *
 * free_one_page():
 *   - takes zone lock
 *   - calls __free_one_page()
 *
 * __free_one_page():
 *
 *   while (buddy exists && same order):
 *       remove buddy
 *       merge
 *       increase order
 *
 *   insert into free_area[order]
 */

/*****************************************************************************/
/* 6. ALLOCATION FAST PATH                                                    */
/*****************************************************************************/

/*
 * get_page_from_freelist():
 *
 *   iterate zonelist:
 *       check cpuset
 *       check watermarks
 *       try buffered_rmqueue()
 *
 * buffered_rmqueue():
 *
 *   order == 0:
 *       use per-cpu pages (fast, no lock)
 *
 *   order > 0:
 *       lock zone
 *       call __rmqueue()
 */

/*****************************************************************************/
/* 7. __rmqueue() — CORE ALLOC                                                */
/*****************************************************************************/

/*
 * for current_order = order → MAX_ORDER:
 *     if free_area[current_order] not empty:
 *         remove block
 *         split using expand()
 *         return page
 */

/*****************************************************************************/
/* 8. expand() — SPLITTING                                                    */
/*****************************************************************************/

/*
 * Split big block into smaller blocks:
 *
 * while high > low:
 *     split into two
 *     add second half to free list
 */

/*****************************************************************************/
/* 9. PER-CPU PAGE CACHE                                                      */
/*****************************************************************************/

/*
 * Purpose:
 *   avoid global locks for order-0 allocations
 *
 * structure:
 *   per_cpu_pages:
 *       - hot list
 *       - cold list
 *
 * flow:
 *   alloc:
 *       take from per-cpu list
 *       if empty → refill from buddy
 *
 *   free:
 *       add to per-cpu list
 *       if too large → bulk free to buddy
 */

/*****************************************************************************/
/* 10. WATERMARKS                                                             */
/*****************************************************************************/

/*
 * zone_watermark_ok():
 *
 * checks if zone has enough free memory
 *
 * watermarks:
 *   pages_min
 *   pages_low
 *   pages_high
 *
 * allocation levels:
 *   LOW  → normal
 *   MIN  → deeper
 *   NO_WATERMARK → emergency
 */

/*****************************************************************************/
/* 11. SLOW PATH (__alloc_pages)                                               */
/*****************************************************************************/

/*
 * If fast path fails:
 *
 *   1. wakeup_kswapd()
 *   2. retry allocation
 *   3. if allowed:
 *         try_to_free_pages()  (direct reclaim)
 *   4. retry again
 *   5. if still fail:
 *         out_of_memory()
 */

/*****************************************************************************/
/* 12. OOM CONNECTION                                                         */
/*****************************************************************************/

/*
 * page_alloc.c DOES NOT kill tasks
 *
 * but it TRIGGERS:
 *
 *   out_of_memory(zonelist, gfp, order)
 *
 * which goes to:
 *   oom_kill.c
 */

/*****************************************************************************/
/* 13. GFP FLAGS EFFECT                                                       */
/*****************************************************************************/

/*
 * __GFP_WAIT → can sleep
 * __GFP_HIGH → use reserves
 * __GFP_NOFAIL → must succeed
 * __GFP_THISNODE → restrict node
 *
 * affects:
 *   - reclaim behavior
 *   - watermark usage
 *   - retry logic
 */

/*****************************************************************************/
/* 14. ZONELISTS (VERY IMPORTANT)                                             */
/*****************************************************************************/

/*
 * zonelist = fallback order of zones
 *
 * Example:
 *   NORMAL → DMA → HIGHMEM
 *
 * NUMA:
 *   local node first
 *   then nearest nodes
 *
 * built by:
 *   build_zonelists()
 */

/*****************************************************************************/
/* 15. NUMA BEHAVIOR                                                          */
/*****************************************************************************/

/*
 * zonelist includes multiple nodes
 *
 * selection based on:
 *   - distance
 *   - node load
 *   - cpuset
 */

/*****************************************************************************/
/* 16. DEBUGGING HELPERS                                                      */
/*****************************************************************************/

/*
 * show_free_areas()
 * si_meminfo()
 * nr_free_pages()
 */

/*****************************************************************************/
/* 17. FINAL TAKEAWAY                                                         */
/*****************************************************************************/

/*
 * page_alloc.c = ENGINE of memory allocation
 *
 * Key ideas:
 *   - buddy allocator (split/merge)
 *   - per-cpu fast path
 *   - zonelist fallback
 *   - watermarks
 *   - reclaim + OOM integration
 */

/*****************************************************************************/
/* END                                                                        */
/*****************************************************************************/

