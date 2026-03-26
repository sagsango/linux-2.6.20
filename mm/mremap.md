/******************************************************************************
 * FILE: linux-2.6.20/mm/mremap.c
 *
 * TITLE:
 *   FULL IDE NOTES — VERY DETAILED SUMMARY + BACKGROUND + FLOW + CONCEPTS
 *
 * SOURCE:
 *   Provided code (linux-2.6.20 mremap.c)
 *   :contentReference[oaicite:0]{index=0}
 *
 ******************************************************************************/

/******************************************************************************
 * 0. VERY DETAILED SUMMARY FIRST (CRITICAL)
 *
 * This file implements:
 *
 *      mremap() system call
 *
 * PURPOSE:
 *
 *      Resize or move an existing memory mapping
 *
 *
 * WHAT mremap CAN DO:
 *
 *   1. SHRINK mapping
 *   2. EXPAND mapping (if space available)
 *   3. MOVE mapping (if MREMAP_MAYMOVE)
 *   4. FORCE move to specific address (MREMAP_FIXED)
 *
 *
 * CORE IDEA:
 *
 *      mmap()  → create mapping
 *      mprotect() → change permissions
 *      mremap() → resize / relocate mapping
 *
 *
 * KEY INSIGHT:
 *
 *      mremap is one of the most complex syscalls in mm/
 *
 *      Because it may:
 *          - modify VMA
 *          - move page tables
 *          - unmap old region
 *          - update accounting
 *
 ******************************************************************************/

/******************************************************************************
 * 1. BACKGROUND — WHY mremap EXISTS?
 *
 * Example:
 *
 *      ptr = mmap(...)
 *      ptr = realloc(ptr, bigger_size)
 *
 * Internally:
 *
 *      mremap() is used to:
 *          - expand allocation
 *          - move if needed
 *
 *
 * WITHOUT mremap:
 *
 *      Need:
 *          mmap(new)
 *          memcpy()
 *          munmap(old)
 *
 *
 * WITH mremap:
 *
 *      Kernel moves page tables directly → NO COPY
 *
 ******************************************************************************/

/******************************************************************************
 * 2. HIGH LEVEL FLOW
 *
 * sys_mremap()
 *     |
 *     v
 * do_mremap()
 *     |
 *     +--> validate args
 *     |
 *     +--> find VMA
 *     |
 *     +--> case 1: shrink
 *     |
 *     +--> case 2: expand in-place
 *     |
 *     +--> case 3: move mapping
 *               |
 *               v
 *           move_vma()
 *               |
 *               +--> copy_vma()
 *               +--> move_page_tables()
 *               +--> do_munmap(old)
 *
 ******************************************************************************/

/******************************************************************************
 * 3. CORE CASES HANDLED BY mremap
 *
 * --------------------------------------------------------------------------
 * CASE 1: SHRINK
 * --------------------------------------------------------------------------
 *
 * old_len >= new_len
 *
 *      → simply unmap extra region
 *
 *      do_munmap(addr + new_len, extra)
 *
 *
 * --------------------------------------------------------------------------
 * CASE 2: EXPAND IN PLACE
 * --------------------------------------------------------------------------
 *
 * Conditions:
 *
 *   - no overlapping next VMA
 *   - enough space available
 *
 * Then:
 *
 *   vma_adjust()
 *   update mm->total_vm
 *
 *
 * --------------------------------------------------------------------------
 * CASE 3: MOVE (RELOCATION)
 * --------------------------------------------------------------------------
 *
 * If cannot expand in place:
 *
 *   → allocate new region
 *   → move page tables
 *   → unmap old region
 *
 ******************************************************************************/

/******************************************************************************
 * 4. FUNCTION: sys_mremap()
 ******************************************************************************/

asmlinkage unsigned long sys_mremap(...)

/*
 * Entry point
 *
 * Steps:
 *
 *   down_write(mmap_sem)
 *   call do_mremap()
 *   up_write(mmap_sem)
 *
 *
 * WHY LOCK?
 *
 *   VMA + page tables are modified
 *
 ******************************************************************************/

/******************************************************************************
 * 5. FUNCTION: do_mremap()
 ******************************************************************************/

unsigned long do_mremap(...)

/*
 * MAIN LOGIC
 *
 *
 * --------------------------------------------------------------------------
 * 5.1 VALIDATION
 * --------------------------------------------------------------------------
 *
 *   - flags valid?
 *   - alignment?
 *   - lengths?
 *   - overflow checks?
 *
 *
 * --------------------------------------------------------------------------
 * 5.2 FIXED REMAP CASE
 * --------------------------------------------------------------------------
 *
 * If MREMAP_FIXED:
 *
 *   → unmap destination first
 *
 *   do_munmap(new_addr)
 *
 *
 * --------------------------------------------------------------------------
 * 5.3 SHRINK CASE
 * --------------------------------------------------------------------------
 *
 * if (old_len >= new_len):
 *
 *   → do_munmap(extra region)
 *
 *
 * --------------------------------------------------------------------------
 * 5.4 FIND VMA
 * --------------------------------------------------------------------------
 *
 *   vma = find_vma(mm, addr)
 *
 * Validate:
 *
 *   - exists
 *   - fully inside VMA
 *   - not crossing boundaries
 *
 *
 * --------------------------------------------------------------------------
 * 5.5 EXPAND IN PLACE
 * --------------------------------------------------------------------------
 *
 * If:
 *
 *   - at end of VMA
 *   - enough space after
 *
 * Then:
 *
 *   vma_adjust()
 *
 *
 * --------------------------------------------------------------------------
 * 5.6 MOVE CASE
 * --------------------------------------------------------------------------
 *
 * If cannot expand:
 *
 *   → allocate new area
 *   → move_vma()
 *
 ******************************************************************************/

/******************************************************************************
 * 6. FUNCTION: move_vma()
 ******************************************************************************/

static unsigned long move_vma(...)

/*
 * MOST IMPORTANT FUNCTION
 *
 * Steps:
 *
 *   1. Create new VMA (copy_vma)
 *   2. Move page tables
 *   3. Unmap old region
 *   4. Update accounting
 *
 *
 * --------------------------------------------------------------------------
 * 6.1 Create new VMA
 * --------------------------------------------------------------------------
 *
 * new_vma = copy_vma(...)
 *
 *
 * --------------------------------------------------------------------------
 * 6.2 Move page tables
 * --------------------------------------------------------------------------
 *
 * move_page_tables(...)
 *
 *
 * --------------------------------------------------------------------------
 * 6.3 Error handling
 * --------------------------------------------------------------------------
 *
 * If partial move fails:
 *
 *   → rollback move
 *
 *
 * --------------------------------------------------------------------------
 * 6.4 Unmap old region
 * --------------------------------------------------------------------------
 *
 * do_munmap(old_addr)
 *
 *
 * --------------------------------------------------------------------------
 * 6.5 Update stats
 * --------------------------------------------------------------------------
 *
 *   mm->total_vm
 *   vm_stat_account()
 *
 ******************************************************************************/

/******************************************************************************
 * 7. FUNCTION: move_page_tables()
 ******************************************************************************/

static unsigned long move_page_tables(...)

/*
 * Moves PTEs in chunks
 *
 * LOOP:
 *
 *   for each PMD-sized chunk:
 *       get old PMD
 *       allocate new PMD
 *       move PTEs
 *
 *
 * LATENCY CONTROL:
 *
 *   extent limited by LATENCY_LIMIT
 *
 *   → prevents long blocking operations
 *
 ******************************************************************************/

/******************************************************************************
 * 8. FUNCTION: move_ptes()
 ******************************************************************************/

static void move_ptes(...)

/*
 * LOW LEVEL — ACTUAL PAGE TABLE MOVE
 *
 * For each PTE:
 *
 *   1. clear old PTE
 *   2. adjust mapping
 *   3. install new PTE
 *
 *
 * IMPORTANT:
 *
 *   ptep_clear_flush()
 *       → removes old mapping + flush
 *
 *   move_pte()
 *       → adjusts PTE metadata
 *
 *   set_pte_at()
 *       → installs new mapping
 *
 *
 * LOCKING:
 *
 *   old_ptl + new_ptl
 *
 *
 * FILE MAPPING CASE:
 *
 *   uses i_mmap_lock to prevent truncate race
 *
 ******************************************************************************/

/******************************************************************************
 * 9. PAGE TABLE WALKING HELPERS
 ******************************************************************************/

/*
 * get_old_pmd():
 *   Walks PGD → PUD → PMD
 *
 * alloc_new_pmd():
 *   Allocates page tables for new location
 *
 */

/******************************************************************************
 * 10. IMPORTANT CONCEPTS
 *
 * --------------------------------------------------------------------------
 * 10.1 Zero-copy remap
 * --------------------------------------------------------------------------
 *
 * mremap avoids copying memory:
 *
 *   → only page tables are moved
 *
 *
 * --------------------------------------------------------------------------
 * 10.2 Page table move vs data move
 * --------------------------------------------------------------------------
 *
 * Data NOT moved
 * Only mapping changes
 *
 *
 * --------------------------------------------------------------------------
 * 10.3 TLB + Cache handling
 * --------------------------------------------------------------------------
 *
 * flush_cache_range()
 * ptep_clear_flush()
 *
 *
 * --------------------------------------------------------------------------
 * 10.4 VMA consistency
 * --------------------------------------------------------------------------
 *
 * VMA updated via:
 *
 *   copy_vma()
 *   vma_adjust()
 *
 *
 * --------------------------------------------------------------------------
 * 10.5 Accounting
 * --------------------------------------------------------------------------
 *
 * Tracks:
 *
 *   mm->total_vm
 *   VM_ACCOUNT
 *
 ******************************************************************************/

/******************************************************************************
 * 11. ASCII FLOW
 *
 * USER:
 *   mremap(old_addr, old_len, new_len)
 *
 * KERNEL:
 *
 *   sys_mremap
 *       |
 *       v
 *   do_mremap
 *       |
 *       +--> validate
 *       |
 *       +--> find VMA
 *       |
 *       +--> if shrink:
 *               do_munmap
 *       |
 *       +--> if expand:
 *               vma_adjust
 *       |
 *       +--> else:
 *               move_vma
 *                   |
 *                   +--> copy_vma
 *                   +--> move_page_tables
 *                           |
 *                           +--> move_ptes
 *                   +--> do_munmap(old)
 *
 ******************************************************************************/

/******************************************************************************
 * 12. INTERVIEW INSIGHTS
 *
 * Q: Why is mremap fast?
 *
 * A:
 *   Because it moves page tables instead of copying memory
 *
 *
 * Q: What is hardest part?
 *
 * A:
 *   Handling:
 *      - partial moves
 *      - rollback on failure
 *      - VMA consistency
 *
 *
 * Q: Why chunked movement?
 *
 * A:
 *   To avoid long kernel latency (scheduler fairness)
 *
 ******************************************************************************/

/******************************************************************************
 * 13. FINAL BIG PICTURE
 *
 * mremap = "virtual memory relocation engine"
 *
 * It combines:
 *
 *   - VMA manipulation (mm layer)
 *   - Page table movement (hardware layer)
 *   - Accounting
 *   - Synchronization
 *
 *
 * This is one of the best examples of:
 *
 *   "How Linux avoids copying memory using MMU"
 *
 ******************************************************************************/
