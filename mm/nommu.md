/*
 * FILE: linux/mm/nommu.c  (Linux 2.6.20)
 *
 * TITLE:
 *   VERY DETAILED IDE NOTES / STUDY FILE
 *
 * GOAL:
 *   Explain the purpose, background, concepts, major data structures,
 *   major flows, and important functions in linux/mm/nommu.c.
 *
 * STYLE:
 *   Written like a single giant IDE text/source file for study.
 *   The intent is not to compile this file, but to read it like code notes.
 */

/*****************************************************************************/
/* 0. BIG PICTURE SUMMARY FIRST                                              */
/*****************************************************************************/

/*
 * linux/mm/nommu.c is the MM subsystem replacement used when the CPU has no
 * MMU.
 *
 * That means:
 *
 *   - no true virtual memory
 *   - no normal page table based address spaces
 *   - no demand paging in the normal MMU sense
 *   - no ordinary mmap behavior where different user VAs can freely map
 *     arbitrary file offsets through page tables
 *   - no normal COW/page-fault driven VM model like linux/mm/memory.c +
 *     linux/mm/mmap.c on MMU systems
 *
 * Instead, this file provides a simplified memory-mapping model suitable for
 * uClinux / NOMMU systems.
 *
 * Core ideas:
 *
 *   1. Mappings are much more concrete / direct.
 *      Often you either:
 *        - allocate real kernel memory with kmalloc and copy file data into it
 *        - or directly map a device / special object if the backing object and
 *          driver allow it.
 *
 *   2. There is no ordinary process-private VA layout machinery like the MMU
 *      version.
 *      The file stores mapping metadata in custom linked lists and a global
 *      rb-tree of shareable VMAs.
 *
 *   3. Sharing semantics are explicitly tracked using vm_usage refcounts and a
 *      global shareable-VMA tree rather than by PTEs and ordinary page tables.
 *
 *   4. Many standard MM helpers either become stubs, become simplified, or are
 *      reinterpreted in NOMMU terms.
 *
 * In short:
 *
 *   linux/mm/nommu.c is not “the same mmap code without page tables”; it is a
 *   different execution model for memory mappings on hardware that cannot do
 *   classic virtual memory.
 */

/*****************************************************************************/
/* 1. WHY THIS FILE EXISTS                                                    */
/*****************************************************************************/

/*
 * On a normal MMU kernel, the Linux VM subsystem relies on:
 *
 *   - per-process page tables
 *   - page faults
 *   - remapping files into user virtual address space
 *   - lazy population
 *   - COW
 *   - TLB invalidation
 *   - normal VMAs describing ranges in a virtual address space that the MMU
 *     enforces
 *
 * On NOMMU systems, the CPU does not provide that abstraction.
 *
 * So Linux still wants to offer APIs like:
 *
 *   mmap()
 *   munmap()
 *   mremap()
 *   brk()
 *   get_user_pages()
 *   access_process_vm()
 *
 * but it must implement them under much stricter constraints.
 *
 * Hence this file replaces large pieces of ordinary MM behavior with a NOMMU
 * interpretation.
 */

/*****************************************************************************/
/* 2. WHAT IS THE MOST IMPORTANT MENTAL MODEL?                                */
/*****************************************************************************/

/*
 * Think of NOMMU mappings as falling into two broad buckets:
 *
 *   A) DIRECT / SHAREABLE mapping
 *      The file/device/driver can expose some real region directly, and we can
 *      share it across users.
 *
 *   B) COPIED mapping
 *      The kernel allocates real memory, copies file contents into it, and the
 *      process uses that.
 *
 * On MMU Linux, mmap usually means “establish VA->page mapping”.
 *
 * On NOMMU Linux, mmap often means:
 *
 *   “find or create a concrete backing buffer/object that this task can use,
 *    then track it in NOMMU VMA bookkeeping.”
 */

/*****************************************************************************/
/* 3. GLOBAL VARIABLES AND THEIR ROLE                                         */
/*****************************************************************************/

/*
 * void *high_memory;
 * struct page *mem_map;
 * unsigned long max_mapnr;
 * unsigned long num_physpages;
 *
 * Familiar global MM symbols still exist because much kernel code expects
 * them, but NOMMU semantics are much simpler.
 *
 * unsigned long askedalloc, realalloc;
 *
 *   Debug/accounting-like counters:
 *     askedalloc = what callers conceptually asked for
 *     realalloc  = actual underlying allocation sizes
 *
 * atomic_t vm_committed_space;
 * int sysctl_overcommit_memory;
 * int sysctl_overcommit_ratio;
 * int sysctl_max_map_count;
 *
 *   Overcommit/accounting state still exists because the kernel still needs
 *   admission control for allocations.
 *
 * int heap_stack_gap = 0;
 *
 *   NOMMU-specific layout behavior knob.
 */

/*****************************************************************************/
/* 4. SHAREABLE VMA TRACKING                                                  */
/*****************************************************************************/

/*
 * struct rb_root nommu_vma_tree = RB_ROOT;
 * DECLARE_RWSEM(nommu_vma_sem);
 *
 * This is one of the most important NOMMU-specific structures in the file.
 *
 * nommu_vma_tree:
 *   Global rb-tree of shareable VMAs.
 *
 * Why global?
 *   Because with no normal page-table-based per-process mapping abstraction,
 *   the kernel needs a concrete global registry of shareable mappings so that
 *   later mmap requests can find compatible existing objects and share them.
 *
 * nommu_vma_sem:
 *   Protects the global tree and related shareable mapping operations.
 */

/*****************************************************************************/
/* 5. generic_file_vm_ops                                                     */
/*****************************************************************************/

/*
 * struct vm_operations_struct generic_file_vm_ops = { };
 *
 * In the MMU world, vm_ops is full of nopage/populate/etc. In NOMMU, most of
 * that machinery is either meaningless or drastically reduced.
 *
 * So this is basically empty here.
 */

/*****************************************************************************/
/* 6. vmtruncate() IN NOMMU                                                   */
/*****************************************************************************/

/*
 * Function:
 *
 *   int vmtruncate(struct inode *inode, loff_t offset)
 *
 * High-level behavior:
 *
 *   - shrink or expand inode->i_size
 *   - on shrink, truncate inode pages
 *   - call inode->i_op->truncate if present
 *   - enforce RLIMIT_FSIZE and s_maxbytes for expansion
 *
 * Important point:
 *
 *   In the MMU version, vmtruncate interacts with page tables, unmap ranges,
 *   and file-backed VMAs more deeply.
 *
 *   In NOMMU, the logic is much smaller because there is no ordinary page table
 *   teardown path of mapped file VMAs in the MMU sense.
 *
 * Flow:
 *
 *   if inode->i_size >= offset:
 *       shrinking path
 *       i_size_write(inode, offset)
 *       truncate_inode_pages(mapping, offset)
 *       call inode truncate op if any
 *
 *   else:
 *       expansion path
 *       check RLIMIT_FSIZE
 *       check s_maxbytes
 *       update i_size
 *       call inode truncate op if any
 */

/*****************************************************************************/
/* 7. kobjsize()                                                              */
/*****************************************************************************/

/*
 * Function:
 *
 *   unsigned int kobjsize(const void *objp)
 *
 * Purpose:
 *   Return actual amount of memory allocated for a kernel object pointer.
 *
 * Why it matters here:
 *   The file uses askedalloc/realalloc accounting to compare requested sizes
 *   versus actual backing allocation sizes.
 *
 * Logic:
 *   - if slab page -> use ksize(objp)
 *   - else derive size from compound allocation order stored in page->index
 *
 * This is particularly useful in NOMMU because mmap-backed private copies are
 * often actual kmalloc allocations, not page-table remaps.
 */

/*****************************************************************************/
/* 8. get_user_pages() IN NOMMU                                               */
/*****************************************************************************/

/*
 * Function:
 *
 *   int get_user_pages(...)
 *
 * Very different mindset from MMU Linux.
 *
 * On MMU Linux, get_user_pages walks page tables and may fault in pages.
 *
 * On NOMMU Linux, there are no normal page tables to walk.
 * So this function becomes:
 *
 *   - find the VMA covering the address
 *   - verify access permissions
 *   - convert the concrete virtual address to struct page with virt_to_page()
 *   - optionally return page/VMA arrays
 *
 * Key details:
 *
 *   vm_flags check:
 *     write ? (VM_WRITE|VM_MAYWRITE) : (VM_READ|VM_MAYREAD)
 *
 *   reject:
 *     VM_IO
 *     VM_PFNMAP
 *     permissions mismatch
 *
 *   page retrieval:
 *     pages[i] = virt_to_page(start)
 *
 * There is no demand-fault machinery here.
 * This is direct and concrete.
 */

/*****************************************************************************/
/* 9. vmalloc/vfree/vmalloc_to_page/vread/vwrite                              */
/*****************************************************************************/

/*
 * vfree(void *addr)
 *   -> simply kfree(addr)
 *
 * __vmalloc(...)
 *   -> really kmalloc(size, adjusted_flags)
 *
 * vmalloc_to_page(addr)
 *   -> virt_to_page(addr)
 *
 * vread/vwrite
 *   -> memcpy wrappers
 *
 * This is a huge conceptual clue:
 *
 *   NOMMU "vmalloc" is not the classic separate virtually contiguous allocator
 *   built on top of page tables. Here it is basically simplified into kmalloc-
 *   backed concrete memory.
 *
 * So many VM helpers become much more direct because “virtual remapping” is not
 * the fundamental mechanism anymore.
 */

/*****************************************************************************/
/* 10. sys_brk() IN NOMMU                                                     */
/*****************************************************************************/

/*
 * Function:
 *
 *   asmlinkage unsigned long sys_brk(unsigned long brk)
 *
 * Very simplified compared to MMU Linux.
 *
 * Checks:
 *   - brk must remain within [start_brk, context.end_brk]
 *   - shrink always allowed
 *   - grow allowed if within that predefined region
 *
 * Notice what is missing:
 *   - no do_brk VM area creation like normal mmap.c path
 *   - no page-table-backed heap expansion
 *
 * In NOMMU, the heap model is much more fixed/simple.
 */

/*****************************************************************************/
/* 11. PER-PROCESS VMA LIST                                                   */
/*****************************************************************************/

/*
 * add_vma_to_mm()
 *
 * Adds a vm_list_struct entry into current->mm->context.vmlist ordered by
 * vm_start.
 *
 * This is very important:
 *
 *   On MMU Linux, mm_struct has rich VMA structures plus rbtree/list managed
 *   in mmap.c.
 *
 *   In NOMMU, the process-local list used here is mm->context.vmlist.
 *
 * So there are effectively two dimensions of tracking:
 *
 *   1. per-process linked list of mappings
 *   2. global rb-tree of shareable VMAs
 */

/*****************************************************************************/
/* 12. find_vma() / find_extend_vma() / find_vma_exact()                      */
/*****************************************************************************/

/*
 * find_vma(mm, addr)
 *   - scans mm->context.vmlist
 *   - returns the VMA if vm_start <= addr < vm_end
 *
 * This is list-based and much simpler than normal MMU tree-based lookup.
 *
 * find_extend_vma(mm, addr)
 *   - just calls find_vma(mm, addr)
 *   - NOMMU does not do stack growth extension here
 *
 * find_vma_exact(mm, addr)
 *   - finds VMA whose vm_start exactly matches addr
 *   - used in do_mremap()
 */

/*****************************************************************************/
/* 13. GLOBAL SHAREABLE VMA TREE HELPERS                                      */
/*****************************************************************************/

/*
 * find_nommu_vma(start)
 *   - search global rb-tree by vm_start
 *
 * add_nommu_vma(vma)
 *   - if file-backed, insert VMA into mapping->i_mmap prio tree
 *   - then insert into global nommu_vma_tree
 *
 * delete_nommu_vma(vma)
 *   - reverse of add_nommu_vma
 *
 * Important conceptual point:
 *
 *   Even in NOMMU mode, file-backed shared objects still participate in
 *   mapping->i_mmap tracking for filesystem/rmap-like visibility where needed.
 *   But the global nommu_vma_tree is the extra NOMMU-level global registry.
 */

/*****************************************************************************/
/* 14. validate_mmap_request() — ONE OF THE MOST IMPORTANT FUNCTIONS          */
/*****************************************************************************/

/*
 * This function decides whether the requested mmap can be supported, and what
 * capabilities the backing object provides.
 *
 * Inputs:
 *   - file / addr / len / prot / flags / pgoff
 * Outputs:
 *   - capabilities bitmask via *_capabilities
 *   - 0 or error
 *
 * Key concepts:
 *
 *   BDI_CAP_MAP_COPY
 *     can copy data into private memory
 *
 *   BDI_CAP_MAP_DIRECT
 *     can directly expose/share mapping
 *
 *   BDI_CAP_READ_MAP / WRITE_MAP / EXEC_MAP
 *     protection-specific support for direct mapping
 *
 * Major logic:
 *
 *   A) Reject fixed-address / overlay mappings on RAM
 *      -> NOMMU cannot generally support arbitrary placement
 *
 *   B) Validate MAP_SHARED / MAP_PRIVATE only
 *
 *   C) Length checks + overflow checks
 *
 *   D) If file-backed:
 *      - file must support mmap
 *      - derive backing capabilities from backing_dev_info or defaults
 *      - regular files / block files default to MAP_COPY
 *      - char devices default to direct map + read/write map capability
 *      - strip unsupported capabilities depending on file ops
 *
 *   E) MAP_SHARED specific checks:
 *      - write permission
 *      - append-only
 *      - locks_verify_locked()
 *      - must support MAP_DIRECT
 *      - prot must be supported directly
 *      - disallow fallback privatization of MAP_SHARED mapping
 *
 *   F) MAP_PRIVATE specific checks:
 *      - need read mode
 *      - may use copy mapping
 *      - if writable private mapping, remove MAP_DIRECT because writable
 *        private direct-share makes no sense
 *
 *   G) exec / noexec mount handling
 *
 *   H) anonymous mapping:
 *      - always considered MAP_COPY capable
 *
 *   I) security_file_mmap()
 *
 * This function is the policy gatekeeper for NOMMU mmap.
 */

/*****************************************************************************/
/* 15. determine_vm_flags()                                                   */
/*****************************************************************************/

/*
 * Purpose:
 *   Convert prot/flags/capabilities into vm_flags.
 *
 * Interesting NOMMU-specific logic:
 *
 *   - If MAP_DIRECT not possible:
 *       allow VM_MAYSHARE only in some readonly/file-backed cases so copies
 *       may still be shared conceptually.
 *
 *   - If direct mapping is possible:
 *       VM_SHARED / VM_MAYSHARE set accordingly.
 *
 *   - If process is being ptraced, private mappings are prevented from being
 *     shareable to avoid breakpoint interference.
 */

/*****************************************************************************/
/* 16. do_mmap_shared_file()                                                  */
/*****************************************************************************/

/*
 * Purpose:
 *   Set up a shared mapping on a file by invoking file->f_op->mmap().
 *
 * Key detail:
 *   If mmap returns -ENOSYS, that is interpreted specially as:
 *
 *     “direct mmap not supported”
 *
 *   not necessarily “hard failure forever”
 *
 * So caller may still fallback to copied private mapping if allowed.
 */

/*****************************************************************************/
/* 17. do_mmap_private()                                                      */
/*****************************************************************************/

/*
 * This is the other major mapping path.
 *
 * For private mappings or anonymous shared mappings, this function often
 * creates a concrete allocated memory buffer.
 *
 * Flow:
 *
 *   1. If file exists, call file->f_op->mmap() first
 *      - if it succeeds, must mean sharing/direct behavior is in place
 *      - if returns -ENOSYS, fallback to copied mapping
 *
 *   2. Allocate memory with:
 *        kmalloc(len, GFP_KERNEL|__GFP_COMP)
 *
 *   3. Set:
 *        vma->vm_start = base
 *        vma->vm_end   = base + len
 *        vma->vm_flags |= VM_MAPPED_COPY
 *
 *   4. If file-backed:
 *        read file contents into allocated buffer
 *        zero-fill tail if short read
 *
 *      Else anonymous:
 *        memset(base, 0, len)
 *
 * This is a huge NOMMU idea:
 *
 *   Instead of mapping file pages into a page table, we may literally allocate
 *   a chunk and read file data into it.
 */

/*****************************************************************************/
/* 18. do_mmap_pgoff() — CENTRAL NOMMU MMAP CREATION PATH                    */
/*****************************************************************************/

/*
 * This is the central function.
 *
 * High-level full flow:
 *
 *   do_mmap_pgoff(file, addr, len, prot, flags, pgoff)
 *     |
 *     +--> validate_mmap_request()
 *     +--> determine_vm_flags()
 *     +--> allocate vm_list_struct
 *     +--> down_write(&nommu_vma_sem)
 *     +--> if sharable, search global nommu_vma_tree for compatible VMA
 *     |      |
 *     |      +--> if exact/acceptable match found:
 *     |              increment vm_usage
 *     |              reuse result
 *     |
 *     +--> if no reusable mapping:
 *             allocate vm_area_struct
 *             init fields
 *             if shared file mapping -> do_mmap_shared_file()
 *             else -> do_mmap_private()
 *             register VMA globally
 *             register VMA in current->mm list
 *     +--> unlock
 *     +--> flush icache if PROT_EXEC
 *     +--> return mapping address
 *
 * Let’s break down the most important parts.
 */

/*****************************************************************************/
/* 18.1 Shared lookup logic                                                   */
/*****************************************************************************/

/*
 * If vm_flags says mapping may be shareable, the code scans nommu_vma_tree.
 *
 * It looks for:
 *   - same inode
 *   - overlapping page offset region
 *
 * Then if overlap is inexact:
 *   - only acceptable for direct-map-capable objects
 *   - otherwise reject with sharing_violation
 *
 * Exact match case:
 *   - vm_pgoff equal
 *   - page length equal
 *   - reuse existing VMA
 *   - atomic_inc(&vma->vm_usage)
 *
 * This is the NOMMU substitute for “same object already mapped, so share it”.
 */

/*****************************************************************************/
/* 18.2 Driver-provided mapping address                                       */
/*****************************************************************************/

/*
 * If direct sharing is intended and file->f_op->get_unmapped_area exists,
 * the driver/file may tell the kernel where the mapping should live.
 *
 * If this returns -ENOSYS:
 *   interpret as “no direct mapping available”, possibly fallback to copy.
 */

/*****************************************************************************/
/* 18.3 VMA allocation and initialization                                     */
/*****************************************************************************/

/*
 * The code allocates and initializes:
 *
 *   struct vm_area_struct *vma
 *   struct vm_list_struct *vml
 *
 * Sets:
 *   vm_file
 *   vm_flags
 *   vm_start
 *   vm_end
 *   vm_pgoff
 *   vm_usage = 1
 *
 * Then chooses setup path:
 *
 *   shared file mapping -> do_mmap_shared_file()
 *   else                -> do_mmap_private()
 */

/*****************************************************************************/
/* 18.4 Registration/accounting                                               */
/*****************************************************************************/

/*
 * On success:
 *
 *   if VM_MAPPED_COPY:
 *       realalloc += kobjsize(buffer)
 *       askedalloc += len
 *
 *   realalloc += kobjsize(vma)
 *   askedalloc += sizeof(*vma)
 *
 *   current->mm->total_vm += len >> PAGE_SHIFT
 *
 *   add_nommu_vma(vma)
 *   add_vma_to_mm(current->mm, vml)
 *
 *   if PROT_EXEC:
 *       flush_icache_range(...)
 *
 * The icache flush is important because executable mappings may have just been
 * copied into RAM and the instruction cache must be coherent.
 */

/*****************************************************************************/
/* 19. put_vma()                                                              */
/*****************************************************************************/

/*
 * This releases a VMA with vm_usage refcounting.
 *
 * Flow:
 *   down_write(nommu_vma_sem)
 *   if atomic_dec_and_test(vm_usage):
 *       delete from global structures
 *       call vm_ops->close if any
 *       if VM_MAPPED_COPY:
 *           kfree(mapped backing buffer)
 *       fput(vm_file)
 *       kfree(vma)
 *   unlock
 *
 * Important point:
 *
 *   Shared mappings are refcounted at the VMA object level in NOMMU.
 */

/*****************************************************************************/
/* 20. do_munmap() / sys_munmap()                                             */
/*****************************************************************************/

/*
 * do_munmap(mm, addr, len)
 *
 * Under NOMMU, unmap is much stricter:
 *
 *   - parameters must match the mapping exactly
 *   - there is no flexible partial VMA split/unmap logic like MMU mmap.c
 *
 * Search:
 *   scan mm->context.vmlist
 *   find VMA whose vm_start == addr and (len==0 or vm_end == addr+len)
 *
 * If found:
 *   put_vma(vml->vma)
 *   unlink vm_list_struct from process list
 *   free vml
 *   reduce total_vm
 *
 * If not found:
 *   return -EINVAL
 *
 * This is a very important NOMMU simplification.
 */

/*****************************************************************************/
/* 21. exit_mmap()                                                            */
/*****************************************************************************/

/*
 * Releases all mappings in mm->context.vmlist.
 *
 * For each entry:
 *   - unlink from list
 *   - put_vma(vma)
 *   - free vm_list_struct
 *
 * Since there is no MMU page-table teardown path, exit_mmap is mostly list
 * cleanup + refcount drops.
 */

/*****************************************************************************/
/* 22. do_brk()                                                               */
/*****************************************************************************/

/*
 * Returns -ENOMEM.
 *
 * Meaning:
 *   The usual anonymous-VMA heap extension path from MMU Linux is not
 *   implemented here in the same way.
 *
 * Combined with the simplified sys_brk behavior earlier, heap management on
 * NOMMU is intentionally constrained.
 */

/*****************************************************************************/
/* 23. do_mremap() / sys_mremap()                                             */
/*****************************************************************************/

/*
 * NOMMU mremap is heavily restricted.
 *
 * Checks:
 *   - new_len != 0
 *   - MREMAP_FIXED cannot move to another address
 *   - VMA must exactly start at addr
 *   - old_len must match VMA size
 *   - VMA must not be shareable (VM_MAYSHARE disallowed)
 *   - new_len must fit inside actual allocated backing object
 *     (new_len <= kobjsize((void *)addr))
 *
 * If okay:
 *   just change vma->vm_end = vm_start + new_len
 *   update askedalloc
 *
 * So NOMMU mremap is really more like:
 *
 *   “resize within already allocated concrete block, without moving”.
 */

/*****************************************************************************/
/* 24. follow_page()                                                          */
/*****************************************************************************/

/*
 * Returns NULL.
 *
 * This again highlights that classic page-table walking semantics are not in
 * play here.
 */

/*****************************************************************************/
/* 25. remap_pfn_range()                                                      */
/*****************************************************************************/

/*
 * Function:
 *   int remap_pfn_range(...)
 *
 * NOMMU behavior here is extremely simplified:
 *   vma->vm_start = vma->vm_pgoff << PAGE_SHIFT;
 *   return 0;
 *
 * This is nothing like the MMU version that installs PFN-backed PTEs.
 *
 * Reason:
 *   Without page tables, “remap_pfn_range” cannot mean the same thing.
 */

/*****************************************************************************/
/* 26. arch_get_unmapped_area() / arch_unmap_area() / unmap_mapping_range()   */
/*****************************************************************************/

/*
 * arch_get_unmapped_area() -> -ENOMEM
 * arch_unmap_area()        -> empty
 * unmap_mapping_range()    -> empty
 *
 * These are stubs or placeholders because the MMU meanings are mostly not
 * applicable in NOMMU mode.
 */

/*****************************************************************************/
/* 27. __vm_enough_memory()                                                   */
/*****************************************************************************/

/*
 * This function is similar in spirit to the MMU version.
 *
 * It still performs overcommit accounting and admission control.
 *
 * Policies:
 *   OVERCOMMIT_ALWAYS
 *   OVERCOMMIT_GUESS
 *   strict ratio-based mode
 *
 * Key idea:
 *   Even NOMMU kernels still need to decide whether there is enough memory to
 *   satisfy a new request.
 *
 * So overcommit logic remains relevant, even though mapping semantics differ.
 */

/*****************************************************************************/
/* 28. filemap_nopage()                                                       */
/*****************************************************************************/

/*
 * BUG()
 *
 * Because NOMMU does not support the normal page-fault-driven nopage model.
 */

/*****************************************************************************/
/* 29. access_process_vm()                                                    */
/*****************************************************************************/

/*
 * This is also simplified.
 *
 * Flow:
 *   - get target mm
 *   - take mmap_sem read lock
 *   - find_vma(mm, addr)
 *   - ensure requested access allowed by vm_flags
 *   - directly copy_to_user / copy_from_user
 *
 * Note:
 *   No get_user_pages/page-table walk like the MMU implementation.
 *   Just direct access within the found concrete mapping.
 */

/*****************************************************************************/
/* 30. IMPORTANT CONTRAST WITH MMU mmap.c / memory.c                          */
/*****************************************************************************/

/*
 * On MMU Linux:
 *
 *   mmap.c + memory.c + filemap.c + rmap + page faults
 *   = VMA metadata + lazy mapping + page-table installation + COW
 *
 * On NOMMU Linux:
 *
 *   nommu.c
 *   = validate mapping request
 *     maybe directly share an object
 *     or allocate/copy a concrete backing buffer
 *     track mapping in per-process list + global shareable tree
 *
 * So a good short phrase is:
 *
 *   MMU mmap maps pages into VA space.
 *   NOMMU mmap often allocates or reuses a concrete RAM object.
 */

/*****************************************************************************/
/* 31. IMPORTANT DATA STRUCTURE RELATIONSHIPS                                 */
/*****************************************************************************/

/*
 * Per-process side:
 *
 *   current->mm->context.vmlist
 *       -> linked list of struct vm_list_struct
 *           -> each points to struct vm_area_struct
 *
 * Global side:
 *
 *   nommu_vma_tree
 *       -> all shareable VMAs
 *
 * File/inode side:
 *
 *   file->f_mapping / inode->i_mapping
 *       -> may also have VMA inserted in mapping->i_mmap
 *
 * Lifetime control:
 *
 *   vm_area_struct::vm_usage
 *       -> refcount for shared reuse
 */

/*****************************************************************************/
/* 32. KEY FLAGS YOU SHOULD NOTICE                                            */
/*****************************************************************************/

/*
 * VM_MAYSHARE
 *   Mapping may be shared / reusable.
 *
 * VM_SHARED
 *   Actively shared mapping semantics.
 *
 * VM_MAPPED_COPY
 *   Very important NOMMU-specific study flag in this file:
 *   means backing memory was allocated/copied and should later be kfree()'d.
 *
 * VM_IO / VM_PFNMAP
 *   Access restrictions still matter.
 */

/*****************************************************************************/
/* 33. MOST IMPORTANT FLOWS TO REMEMBER                                       */
/*****************************************************************************/

/*
 * FLOW A: Private file mmap on NOMMU
 * ----------------------------------
 *
 *   user mmap(file, MAP_PRIVATE)
 *      -> validate_mmap_request
 *      -> maybe no direct map possible
 *      -> do_mmap_private
 *      -> kmalloc(len)
 *      -> read file contents into buffer
 *      -> register VMA
 *      -> return concrete address
 *
 *
 * FLOW B: Shared direct-capable device/file mapping
 * -------------------------------------------------
 *
 *   user mmap(file, MAP_SHARED)
 *      -> validate capabilities
 *      -> search nommu_vma_tree for reusable compatible VMA
 *      -> if found: inc vm_usage and reuse
 *      -> else call file->f_op->mmap / maybe get_unmapped_area
 *      -> register globally + per-mm
 *
 *
 * FLOW C: munmap on NOMMU
 * -----------------------
 *
 *   user munmap(addr,len)
 *      -> exact-match lookup in mm->context.vmlist
 *      -> put_vma()
 *      -> if refcount reaches zero, free backing buffer if copied
 *      -> unlink metadata
 *
 *
 * FLOW D: mremap on NOMMU
 * -----------------------
 *
 *   user mremap(addr, old_len, new_len)
 *      -> must match exact VMA
 *      -> non-shareable only
 *      -> new_len must fit in actual allocated object size
 *      -> update vm_end only
 */

/*****************************************************************************/
/* 34. WHAT IS “DIRECT” VS “COPY” IN ONE LINE?                                */
/*****************************************************************************/

/*
 * DIRECT:
 *   expose/share backing object itself
 *
 * COPY:
 *   allocate RAM and copy file/object contents into it
 */

/*****************************************************************************/
/* 35. WHY THE GLOBAL TREE MATTERS                                            */
/*****************************************************************************/

/*
 * Without normal page-table-based sharing, the kernel needs an explicit global
 * structure to find already-created shareable mappings.
 *
 * That is one of the core NOMMU design substitutions.
 */

/*****************************************************************************/
/* 36. WHY vm_usage MATTERS                                                   */
/*****************************************************************************/

/*
 * In MMU Linux, lots of sharing state is naturally represented by page tables,
 * page refcounts, reverse mapping, etc.
 *
 * In NOMMU Linux, this file often shares an entire VMA/backing object by
 * reusing the same concrete mapping object across multiple users.
 *
 * Therefore vm_usage is a key ownership/lifetime mechanism.
 */

/*****************************************************************************/
/* 37. THINGS THAT ARE STUBBED / NON-EQUIVALENT TO MMU LINUX                  */
/*****************************************************************************/

/*
 * follow_page()         -> NULL
 * filemap_nopage()      -> BUG()
 * do_brk()              -> -ENOMEM
 * arch_get_unmapped_area()-> -ENOMEM
 * unmap_mapping_range() -> empty
 * vmap()/vunmap()       -> BUG()
 * remap_pfn_range()     -> drastically simplified
 *
 * This tells you exactly which VM abstractions do not make sense in the
 * ordinary MMU interpretation on NOMMU systems.
 */

/*****************************************************************************/
/* 38. INTERVIEW / REVIEW STYLE SUMMARY                                       */
/*****************************************************************************/

/*
 * If someone asks:
 *
 *   “What does linux/mm/nommu.c do?”
 *
 * Good answer:
 *
 *   It replaces normal MMU-based mmap/VM behavior with a NOMMU-compatible
 *   model where mappings are either directly shared from capable objects or
 *   backed by concrete allocated/copy buffers, tracked through per-process
 *   mapping lists and a global shareable-VMA tree.
 *
 * If someone asks:
 *
 *   “What is the most important difference from mm/mmap.c?”
 *
 * Good answer:
 *
 *   On MMU kernels, mmap mostly establishes virtual mappings and page-fault-
 *   driven behavior. On NOMMU kernels, mmap often becomes an object creation/
 *   reuse problem over concrete RAM buffers or directly shareable device/file
 *   regions.
 */

/*****************************************************************************/
/* 39. FINAL TAKEAWAY                                                         */
/*****************************************************************************/

/*
 * linux/mm/nommu.c is best understood as:
 *
 *   “A compatibility implementation of Linux memory-mapping APIs for systems
 *    that do not have true virtual memory, achieved by replacing page-table
 *    semantics with explicit concrete-object allocation, sharing, and tracking.”
 *
 * Keep these four anchors in mind:
 *
 *   1. no true page-table VM model
 *   2. concrete copied buffers are common
 *   3. global shareable VMA tree is central
 *   4. many familiar MM functions become simplified or stubbed
 */

/*****************************************************************************/
/* END OF NOTES                                                               */
/*****************************************************************************/

