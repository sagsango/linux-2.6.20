/******************************************************************************
 * FILE: linux-2.6.20/mm/msync.c
 *
 * TITLE:
 *   FULL IDE NOTES — VERY DETAILED SUMMARY + BACKGROUND + FLOW + CONCEPTS
 *
 * PURPOSE:
 *   Explain the Linux 2.6.20 source file mm/msync.c in a "one IDE text file"
 *   style, with detailed summary first, then background, control flow,
 *   function-by-function behavior, design intent, corner cases, and mental
 *   model.
 *
 ******************************************************************************/

/******************************************************************************
 * 0. VERY DETAILED SUMMARY FIRST
 *
 * This file implements:
 *
 *      msync()
 *
 * System call:
 *
 *      sys_msync(unsigned long start, size_t len, int flags)
 *
 *
 * WHAT msync DOES:
 *
 *   It synchronizes a memory-mapped region with the underlying file.
 *
 *
 * SIMPLE USER VIEW:
 *
 *   Suppose a process did:
 *
 *      fd = open("x", ...);
 *      p = mmap(fd, ... MAP_SHARED ...);
 *      p[0] = 'A';
 *
 *   Those writes may still be only in memory/page cache for some time.
 *
 *   msync() gives the process a way to ask:
 *
 *      "please synchronize this mapping with storage"
 *
 *
 * IMPORTANT SEMANTICS IN THIS FILE:
 *
 *   1. MS_SYNC
 *        Synchronous sync.
 *        For shared file mappings, it calls do_fsync(file, 0).
 *
 *   2. MS_ASYNC
 *        In this kernel version, it does basically nothing.
 *        Dirty pages are already tracked by the kernel.
 *        It does NOT explicitly start I/O here anymore.
 *
 *   3. MS_INVALIDATE
 *        Requests invalidation semantics, but if VMA is VM_LOCKED,
 *        kernel returns -EBUSY.
 *
 *
 * VERY IMPORTANT HIGH-LEVEL BEHAVIOR:
 *
 *   - The syscall walks VMAs covering [start, end)
 *   - Unmapped holes are tolerated, but remembered
 *   - At the end, if everything else succeeded, the syscall may still
 *     return -ENOMEM if any hole was encountered
 *   - Only file-backed shared mappings matter for MS_SYNC
 *   - Anonymous mappings are not fsync'd
 *
 *
 * THE MAIN DESIGN IDEA:
 *
 *   msync() is NOT directly writing individual PTEs/pages here.
 *   Instead, for MS_SYNC on shared file mappings, it delegates to:
 *
 *      do_fsync(file, 0)
 *
 *   meaning:
 *
 *      "sync the underlying file object"
 *
 *
 * WHY THIS FILE IS SHORT:
 *
 *   Because most real syncing work is delegated to the filesystem/writeback
 *   layer. This file mainly:
 *
 *      - validates arguments
 *      - walks VMAs
 *      - checks mapping type / lock state
 *      - decides when to call do_fsync()
 *
 ******************************************************************************/

/******************************************************************************
 * 1. BACKGROUND — WHY msync EXISTS
 *
 * mmap() creates a mapping between a process virtual address range and
 * some backing object, usually a file.
 *
 * There are two broad useful cases:
 *
 *   A) MAP_PRIVATE
 *        Writes are private/COW, not meant to update the file directly
 *
 *   B) MAP_SHARED
 *        Writes are shared and are intended to become visible in the file
 *
 *
 * For MAP_SHARED, user space often wants control over durability / timing.
 *
 * Example:
 *
 *   database
 *   log writer
 *   shared-memory file-backed structure
 *   persistent memory-like usage model
 *
 * User may want:
 *
 *   "I modified bytes in this mapped region. Push them out now."
 *
 * That is exactly where msync() comes in.
 *
 ******************************************************************************/

/******************************************************************************
 * 2. WHAT THIS FILE DOES NOT DO
 *
 * This file does NOT:
 *
 *   - manually scan page tables and write pages itself
 *   - flush individual cachelines directly
 *   - implement filesystem-specific writeback
 *   - decide exact disk scheduling
 *
 * Instead it acts as:
 *
 *   syscall policy + VMA walker + delegation layer
 *
 ******************************************************************************/

/******************************************************************************
 * 3. FILE HEADER COMMENTS — VERY IMPORTANT
 *
 * The comments at top explain an important historical evolution:
 *
 *   - MS_SYNC:
 *       sync entire file, including mappings
 *
 *   - MS_ASYNC:
 *       older kernels used to start async I/O
 *       but by this version, it no longer does so
 *
 * Why?
 *
 *   Because dirty-page tracking is already correct.
 *   So user has flexibility:
 *
 *      - fsync() if it wants synchronous completion
 *      - fadvise(FADV_DONTNEED) if it wants another behavior
 *
 * So this file deliberately makes MS_ASYNC mostly a no-op.
 *
 * That comment is one of the key conceptual points of the whole source.
 *
 ******************************************************************************/

/******************************************************************************
 * 4. ONLY ONE FUNCTION HERE
 *
 * Main function:
 *
 *      asmlinkage long sys_msync(unsigned long start, size_t len, int flags)
 *
 * So all behavior is concentrated in one syscall implementation.
 *
 ******************************************************************************/

/******************************************************************************
 * 5. HIGH LEVEL FLOW
 *
 *   user
 *     |
 *     v
 *   sys_msync(start, len, flags)
 *     |
 *     +--> validate flags
 *     +--> validate alignment
 *     +--> round length to page size
 *     +--> compute end = start + len
 *     +--> if empty range, return 0
 *     |
 *     +--> take mm->mmap_sem for read
 *     +--> find first VMA covering/after start
 *     +--> walk VMAs across [start, end)
 *             |
 *             +--> handle holes
 *             +--> reject MS_INVALIDATE on VM_LOCKED
 *             +--> if MS_SYNC and shared file mapping:
 *                     call do_fsync(file, 0)
 *     |
 *     +--> return 0 or final error / unmapped_error
 *
 ******************************************************************************/

/******************************************************************************
 * 6. FUNCTION WALKTHROUGH — sys_msync()
 ******************************************************************************/

/*
 * Prototype:
 *
 *   asmlinkage long sys_msync(unsigned long start, size_t len, int flags)
 *
 * Parameters:
 *
 *   start:
 *      start address of memory range
 *
 *   len:
 *      byte length
 *
 *   flags:
 *      MS_ASYNC / MS_SYNC / MS_INVALIDATE
 */

/******************************************************************************
 * 6.1 Local variables
 ******************************************************************************/

/*
 * unsigned long end;
 * struct mm_struct *mm = current->mm;
 * struct vm_area_struct *vma;
 * int unmapped_error = 0;
 * int error = -EINVAL;
 *
 * Meaning:
 *
 *   end:
 *      end address after rounding length
 *
 *   mm:
 *      current process address space descriptor
 *
 *   vma:
 *      current VMA while walking
 *
 *   unmapped_error:
 *      remembers whether there was a hole in requested range
 *
 *   error:
 *      current syscall return state
 */

/******************************************************************************
 * 6.2 Flag validation
 ******************************************************************************/

/*
 * if (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC))
 *     goto out;
 *
 * Meaning:
 *
 *   Reject unknown flags.
 *
 *
 * if (start & ~PAGE_MASK)
 *     goto out;
 *
 * Meaning:
 *
 *   Start address must be page-aligned.
 *
 *   msync works on page-granularity regions.
 *
 *
 * if ((flags & MS_ASYNC) && (flags & MS_SYNC))
 *     goto out;
 *
 * Meaning:
 *
 *   Cannot request both async and sync simultaneously.
 *
 ******************************************************************************/

/******************************************************************************
 * 6.3 Length rounding and overflow handling
 ******************************************************************************/

/*
 * error = -ENOMEM;
 * len = (len + ~PAGE_MASK) & PAGE_MASK;
 * end = start + len;
 * if (end < start)
 *     goto out;
 *
 * Meaning:
 *
 *   - length rounded up to page size
 *   - compute end address
 *   - detect wraparound/overflow
 *
 *
 * Why -ENOMEM on bad range?
 *
 *   Because conceptually the requested address interval becomes invalid
 *   in process address space terms.
 */

/******************************************************************************
 * 6.4 Empty range handling
 ******************************************************************************/

/*
 * error = 0;
 * if (end == start)
 *     goto out;
 *
 * Meaning:
 *
 *   Zero-length effective range is a success no-op.
 */

/******************************************************************************
 * 6.5 Acquire mmap semaphore
 ******************************************************************************/

/*
 * down_read(&mm->mmap_sem);
 *
 * Why read lock?
 *
 *   We only need to walk VMAs safely.
 *
 *   No VMA modifications are being done directly by msync itself.
 *
 *   But we must prevent concurrent VMA structure changes while walking.
 */

/******************************************************************************
 * 6.6 Find starting VMA
 ******************************************************************************/

/*
 * vma = find_vma(mm, start);
 *
 * Meaning:
 *
 *   Find first VMA whose vm_end > start.
 *
 *   That VMA may:
 *      - contain start
 *      - or begin after start (meaning there is a hole first)
 */

/******************************************************************************
 * 6.7 Main VMA walking loop
 ******************************************************************************/

/*
 * for (;;) {
 *     struct file *file;
 *     ...
 * }
 *
 * This loop walks across all VMAs covering the requested interval.
 *
 * The kernel keeps advancing "start" until start >= end.
 */

/******************************************************************************
 * 7. HOLE HANDLING
 ******************************************************************************/

/*
 * error = -ENOMEM;
 * if (!vma)
 *     goto out_unlock;
 *
 * Meaning:
 *
 *   If there is no VMA at all while we still have address range left,
 *   then remaining interval is unmapped.
 *
 *
 * if (start < vma->vm_start) {
 *     start = vma->vm_start;
 *     if (start >= end)
 *         goto out_unlock;
 *     unmapped_error = -ENOMEM;
 * }
 *
 * Meaning:
 *
 *   There is an unmapped hole from current start up to vma->vm_start.
 *
 * Behavior is important:
 *
 *   - do NOT fail immediately
 *   - skip hole
 *   - remember that at least one hole existed
 *
 * Then continue processing mapped regions.
 *
 *
 * WHY?
 *
 *   This syscall intentionally tolerates partially unmapped ranges,
 *   but reports that fact at the end with -ENOMEM if no other stronger
 *   error happened.
 *
 *
 * So semantic model is:
 *
 *   "Do as much as possible on mapped parts,
 *    but tell caller range wasn't fully mapped."
 */

/******************************************************************************
 * 8. VM_LOCKED + MS_INVALIDATE CHECK
 ******************************************************************************/

/*
 * if ((flags & MS_INVALIDATE) &&
 *         (vma->vm_flags & VM_LOCKED)) {
 *     error = -EBUSY;
 *     goto out_unlock;
 * }
 *
 * Meaning:
 *
 *   You cannot invalidate a locked mapping.
 *
 *
 * Why?
 *
 *   VM_LOCKED means pages are pinned/resident by mlock semantics.
 *
 *   Invalidating such a mapping conflicts with the intent of keeping it
 *   resident and stable.
 *
 *
 * So:
 *
 *   MS_INVALIDATE + locked VMA => -EBUSY
 */

/******************************************************************************
 * 9. FILE EXTRACTION
 ******************************************************************************/

/*
 * file = vma->vm_file;
 *
 * Meaning:
 *
 *   If VMA is file-backed, file != NULL
 *   If anonymous, file == NULL
 */

/******************************************************************************
 * 10. START ADVANCE
 ******************************************************************************/

/*
 * start = vma->vm_end;
 *
 * Meaning:
 *
 *   By default, after processing this VMA, next range starts from the
 *   end of this VMA.
 */

/******************************************************************************
 * 11. REAL WORK CASE: MS_SYNC + file-backed shared mapping
 ******************************************************************************/

/*
 * if ((flags & MS_SYNC) && file &&
 *         (vma->vm_flags & VM_SHARED)) {
 *     ...
 * }
 *
 * This is the main meaningful work in this file.
 *
 * Conditions:
 *
 *   1. caller requested MS_SYNC
 *   2. region is file-backed
 *   3. VMA is shared
 *
 *
 * Why VM_SHARED?
 *
 *   Because private mappings are not the normal "push back to file"
 *   case.
 *
 *   MAP_PRIVATE writes are COW/private and not intended to directly
 *   update underlying file contents.
 *
 *
 * So only shared file mappings are synchronized this way.
 */

/******************************************************************************
 * 11.1 Reference management around do_fsync
 ******************************************************************************/

/*
 * get_file(file);
 * up_read(&mm->mmap_sem);
 * error = do_fsync(file, 0);
 * fput(file);
 *
 * Why get_file()?
 *
 *   Because once mmap_sem is dropped, the VMA/file association could change
 *   or disappear due to concurrent activity.
 *
 *   We need an extra file reference so file stays alive.
 *
 *
 * Why drop mmap_sem before do_fsync()?
 *
 *   Because do_fsync() may sleep / block / perform substantial work.
 *
 *   Holding mmap_sem across that would be bad for concurrency and latency.
 *
 *
 * So pattern is:
 *
 *   - grab file reference
 *   - drop mmap_sem
 *   - fsync underlying file
 *   - drop file reference
 */

/******************************************************************************
 * 11.2 Result after do_fsync()
 ******************************************************************************/

/*
 * if (error || start >= end)
 *     goto out;
 *
 * Meaning:
 *
 *   If fsync failed, return that error immediately.
 *
 *   Or if this was the last part of range, done.
 */

/******************************************************************************
 * 11.3 Reacquire mmap_sem and resume walk
 ******************************************************************************/

/*
 * down_read(&mm->mmap_sem);
 * vma = find_vma(mm, start);
 *
 * Important:
 *
 *   Since lock was dropped, cannot safely continue with old vma pointer.
 *
 * Must restart lookup from current address.
 *
 * This is a classic lock-drop / revalidate pattern.
 */

/******************************************************************************
 * 12. NON-MS_SYNC OR NON-SHARED CASE
 ******************************************************************************/

/*
 * else {
 *     if (start >= end) {
 *         error = 0;
 *         goto out_unlock;
 *     }
 *     vma = vma->vm_next;
 * }
 *
 * Meaning:
 *
 *   If no fsync needed for this VMA, just keep walking.
 *
 * This includes:
 *
 *   - MS_ASYNC
 *   - anonymous mappings
 *   - file-backed but not shared mappings
 *   - potentially other non-actionable VMA types
 */

/******************************************************************************
 * 13. EXIT PATHS
 ******************************************************************************/

/*
 * out_unlock:
 *     up_read(&mm->mmap_sem);
 *
 * out:
 *     return error ? : unmapped_error;
 *
 *
 * Very important final return rule:
 *
 *   If "error" is nonzero, return it.
 *   Otherwise return unmapped_error.
 *
 *
 * This means:
 *
 *   - a hard error like -EBUSY or fsync failure wins
 *   - if no hard error but holes were seen, return -ENOMEM
 *   - if no hard error and no holes, return 0
 */

/******************************************************************************
 * 14. FULL RETURN SEMANTICS
 *
 * --------------------------------------------------------------------------
 * 14.1 return 0
 * --------------------------------------------------------------------------
 *
 *   - valid range
 *   - no fatal errors
 *   - mapped parts processed
 *   - no unmapped holes
 *
 *
 * --------------------------------------------------------------------------
 * 14.2 return -EINVAL
 * --------------------------------------------------------------------------
 *
 *   - bad flags
 *   - unaligned start
 *   - MS_ASYNC and MS_SYNC both set
 *
 *
 * --------------------------------------------------------------------------
 * 14.3 return -ENOMEM
 * --------------------------------------------------------------------------
 *
 *   - overflow / invalid address interval
 *   - range partially or fully unmapped
 *
 *
 * --------------------------------------------------------------------------
 * 14.4 return -EBUSY
 * --------------------------------------------------------------------------
 *
 *   - MS_INVALIDATE requested on locked mapping
 *
 *
 * --------------------------------------------------------------------------
 * 14.5 filesystem error
 * --------------------------------------------------------------------------
 *
 *   Returned directly from do_fsync(file, 0)
 *
 ******************************************************************************/

/******************************************************************************
 * 15. CONCEPTUAL MODEL OF FLAGS
 *
 * --------------------------------------------------------------------------
 * MS_SYNC
 * --------------------------------------------------------------------------
 *
 *   "Synchronize now and wait"
 *
 * In this code:
 *   for each shared file-backed VMA in range:
 *       do_fsync(file, 0)
 *
 *
 * --------------------------------------------------------------------------
 * MS_ASYNC
 * --------------------------------------------------------------------------
 *
 *   Historical compatibility flag
 *
 * In this version:
 *   mostly no-op here
 *
 * Reason:
 *   dirty tracking already exists
 *
 *
 * --------------------------------------------------------------------------
 * MS_INVALIDATE
 * --------------------------------------------------------------------------
 *
 *   Request invalidation-style behavior
 *
 * In this file:
 *   only explicit special handling is:
 *       reject if VMA is VM_LOCKED
 *
 ******************************************************************************/

/******************************************************************************
 * 16. IMPORTANT DESIGN OBSERVATIONS
 *
 * --------------------------------------------------------------------------
 * 16.1 msync works at VMA/file abstraction level, not page level here
 * --------------------------------------------------------------------------
 *
 * The code does not iterate page-by-page.
 *
 * Instead:
 *   walk VMAs
 *   call do_fsync(file, 0)
 *
 *
 * --------------------------------------------------------------------------
 * 16.2 Same file may be fsync'd multiple times
 * --------------------------------------------------------------------------
 *
 * If multiple adjacent/overlapping VMAs map same file, this code may call
 * do_fsync multiple times as it walks.
 *
 * This source is simple and correctness-focused, not aggressively optimized
 * for deduping per-file calls in this path.
 *
 *
 * --------------------------------------------------------------------------
 * 16.3 mmap_sem is intentionally dropped around fsync
 * --------------------------------------------------------------------------
 *
 * Very important for scalability and avoiding lock hold during slow I/O.
 *
 *
 * --------------------------------------------------------------------------
 * 16.4 Hole-tolerant walking
 * --------------------------------------------------------------------------
 *
 * This is a subtle semantic detail:
 *
 *   msync does not immediately fail on holes
 *   it records unmapped_error and continues
 *
 ******************************************************************************/

/******************************************************************************
 * 17. ASCII FLOW
 *
 * USER:
 *   msync(start, len, flags)
 *
 * KERNEL:
 *
 *   sys_msync
 *      |
 *      +--> validate flags/alignment
 *      +--> round len
 *      +--> down_read(mmap_sem)
 *      +--> vma = find_vma(start)
 *      |
 *      +--> loop across [start, end):
 *              |
 *              +--> if hole:
 *              |       unmapped_error = -ENOMEM
 *              |       skip to next VMA
 *              |
 *              +--> if MS_INVALIDATE && VM_LOCKED:
 *              |       return -EBUSY
 *              |
 *              +--> if MS_SYNC && file-backed && VM_SHARED:
 *              |       get_file(file)
 *              |       up_read(mmap_sem)
 *              |       do_fsync(file, 0)
 *              |       fput(file)
 *              |       down_read(mmap_sem)
 *              |       vma = find_vma(start)
 *              |
 *              +--> else:
 *                      vma = vma->vm_next
 *      |
 *      +--> return error ? : unmapped_error
 *
 ******************************************************************************/

/******************************************************************************
 * 18. MENTAL CONNECTION TO OTHER mm FILES
 *
 * This file fits with other mm files like this:
 *
 *   mmap.c
 *      creates VMAs / mappings
 *
 *   memory.c / filemap.c
 *      handle page faults, page cache, actual pages
 *
 *   msync.c
 *      user-triggered synchronization request on mapped range
 *
 *   file writeback / fsync layer
 *      does actual storage sync
 *
 ******************************************************************************/

/******************************************************************************
 * 19. HOW TO THINK ABOUT THIS FILE IN ONE SENTENCE
 *
 *   "msync.c is a VMA walker that validates the request and, for shared
 *    file mappings, delegates synchronization to fsync on the underlying
 *    file while carefully handling holes and lock-dropping."
 *
 ******************************************************************************/

/******************************************************************************
 * 20. INTERVIEW / REVIEW NOTES
 *
 * Q: Why is msync.c so small?
 *
 * A:
 *   Because most heavy lifting is delegated to filesystem writeback;
 *   this layer mainly performs policy and mapping traversal.
 *
 *
 * Q: Why drop mmap_sem before do_fsync?
 *
 * A:
 *   fsync can block for long time; keeping mmap_sem would hurt concurrency.
 *
 *
 * Q: Why only VM_SHARED + file-backed matters for MS_SYNC?
 *
 * A:
 *   Because only shared file mappings are naturally synchronized back to
 *   underlying file object this way.
 *
 *
 * Q: Why remember unmapped_error instead of failing immediately?
 *
 * A:
 *   The syscall is designed to process mapped subranges when possible,
 *   but still report that the full requested interval was not valid.
 *
 ******************************************************************************/

/******************************************************************************
 * 21. FINAL BIG PICTURE
 *
 * msync.c is small, but conceptually important.
 *
 * It shows a classic Linux MM pattern:
 *
 *   - validate user request
 *   - walk VMAs
 *   - apply policy based on mapping type
 *   - delegate real work to lower subsystem
 *   - avoid holding major lock across slow operation
 *
 * This file is less about page-table tricks and more about:
 *
 *   correctness of semantics
 *   VMA traversal
 *   clean syscall behavior
 *   proper interaction between MM and FS layers
 *
 ******************************************************************************/
