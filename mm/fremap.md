================================================================================
FILE: linux_mm_fremap_background_and_flow.txt
TOPIC: fremap.c - explicit page table population and nonlinear file mappings
SOURCE: user-provided code :contentReference[oaicite:0]{index=0}
KERNEL ERA: Linux 2.6.x style VM
================================================================================


SECTION 1: WHAT THIS FILE DOES
================================================================================

This file implements support for:

    1. explicit page table population for file-backed VMAs
    2. nonlinear file mappings
    3. remap_file_pages() syscall support

The key idea is:

    a process already has a shared file mapping,
    and wants pages from arbitrary file offsets to appear at chosen
    virtual addresses inside that same VMA,
    without creating new VMAs.

So this file is about:

    "change what file page a virtual page points to,
     mostly by manipulating PTEs directly"

This is different from normal mmap(), where mapping is usually linear:

    virtual offset X  -> file offset X

This file supports:

    virtual offset A  -> file offset B
    virtual offset A+1 page -> file offset Z
    virtual offset A+2 page -> file offset Q
    ...

inside the same VMA.



SECTION 2: WHY THIS EXISTS
================================================================================

Normal mmap() is linear:

    file page N  <-> virtual page base + N

But some applications want to use one fixed virtual window and remap different
parts of a large file into it.

Instead of:

    unmap old range
    mmap new range
    create/split many VMAs

the kernel offers:

    remap_file_pages()

which works mostly by page table manipulation.

Benefits:

    fewer VMA changes
    efficient remapping within one existing VMA
    works nicely for large shared mappings
    avoids repeated mmap()/munmap() churn



SECTION 3: CORE CONCEPTS USED HERE
================================================================================

This file revolves around four key concepts:

1) present PTE
    real mapped page is currently installed in page table

2) file PTE / non-present file encoding
    not a real mapped page
    instead encodes which file offset should be faulted in later

3) populate()
    VMA operation that prefaults or explicitly installs pages/PTEs

4) VM_NONLINEAR
    marks that the VMA no longer follows ordinary linear
    virtual-offset -> file-offset mapping

So the flow is often:

    user asks to remap file pages
        -> kernel validates VMA
        -> if nonlinear mapping is needed, mark VMA VM_NONLINEAR
        -> call vma->vm_ops->populate()
        -> populate either installs real page PTEs
           or installs file-PTE placeholders



SECTION 4: HIGH LEVEL MAP OF FUNCTIONS
================================================================================

This file contains three major pieces:

    zap_pte()
        remove old mapping at one address safely

    install_page()
        install a real file-backed page into a user PTE

    install_file_pte()
        install a special non-present "file offset" PTE

    sys_remap_file_pages()
        user-facing syscall that drives nonlinear remapping

So a simple summary is:

    zap old entry
    install new mapping info
    maybe mark VMA nonlinear



SECTION 5: zap_pte()
================================================================================

Function:

    static int zap_pte(struct mm_struct *mm, struct vm_area_struct *vma,
                       unsigned long addr, pte_t *ptep)

Purpose:

    remove whatever mapping currently exists at one virtual address

This is the local helper used before installing a new mapping.

It handles two cases:

    A) present PTE
    B) non-present PTE (swap/file-style encoded entry)

Return value:

    returns 1 if a normal struct page-backed mapping was removed
    returns 0 otherwise

That return value matters for RSS accounting later.



SECTION 6: zap_pte() - PRESENT PTE CASE
================================================================================

If:

    pte_present(pte)

then there is an actual mapped page in memory.

The function does:

    flush_cache_page(vma, addr, pte_pfn(pte))
    pte = ptep_clear_flush(vma, addr, ptep)
    page = vm_normal_page(vma, addr, pte)

Meaning:

1. flush virtual cache aliases if architecture needs it
2. clear and flush the PTE/TLB mapping
3. find the struct page corresponding to that mapping if it is a normal page

Then if page exists:

    if dirty -> set_page_dirty(page)
    page_remove_rmap(page, vma)
    page_cache_release(page)

So this is basically:

    "detach this virtual mapping from the page, preserve dirty state,
     remove reverse mapping, drop reference"



SECTION 7: zap_pte() - NONPRESENT PTE CASE
================================================================================

Else branch:

    if (!pte_file(pte))
        free_swap_and_cache(pte_to_swp_entry(pte));
    pte_clear_not_present_full(mm, addr, ptep, 0);

This means the PTE is not present, but could represent:

    swap entry
    file entry (nonlinear file PTE)
    migration-like non-present encoding depending on subsystem

If it is not a file PTE:

    free swap entry and cache state

Then clear the non-present entry fully.

So zap_pte() is careful to clean up both:

    normal mapped pages
    metadata-only nonpresent entries



SECTION 8: WHY zap_pte() RETURNS !!page
================================================================================

At the end:

    return !!page;

Why?

Because if a real normal page mapping was removed, RSS accounting may need to
change.

If it was only a file-encoded or swap-like non-present entry, then there is no
normal resident file page counted in file_rss in the same way.

So callers use this to decide whether:

    inc_mm_counter(mm, file_rss)
or
    dec_mm_counter(mm, file_rss)

is needed when replacing entries.



SECTION 9: install_page()
================================================================================

Function:

    int install_page(struct mm_struct *mm, struct vm_area_struct *vma,
                     unsigned long addr, struct page *page, pgprot_t prot)

Purpose:

    install a real file-backed page into the process page tables at addr

Think of it as:

    "map this actual struct page at this user address"

This is the "materialized mapping" path.



SECTION 10: install_page() FLOW
================================================================================

High-level flow:

    get_locked_pte(mm, addr, &ptl)
    validate page still belongs to file and is not truncated
    check mapcount overflow guard
    zap old entry if needed
    maybe increase file_rss
    create PTE from page + protection
    set_pte_at()
    add file rmap
    update MMU cache
    done

This is a classic page-table installation path with file-backed accounting.



SECTION 11: install_page() - GETTING THE PTE
================================================================================

First:

    pte = get_locked_pte(mm, addr, &ptl)

This ensures:

    page table exists for addr
    caller gets locked PTE pointer
    safe to modify that entry

If this fails:

    return -ENOMEM

So PTE allocation/locking is step 1.



SECTION 12: install_page() - TRUNCATION SAFETY CHECK
================================================================================

Important section:

    inode = vma->vm_file->f_mapping->host;
    size = (i_size_read(inode) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
    if (!page->mapping || page->index >= size)
        goto unlock with -EINVAL

Why?

Because the page passed in may have become invalid relative to the file:

    file may have been truncated
    page may have been detached from mapping
    page index may now lie beyond EOF

So install_page() refuses to install a stale page into userspace.

This is important because page cache / file size can change concurrently.



SECTION 13: install_page() - MAPCOUNT OVERFLOW GUARD
================================================================================

This line:

    if (page_mapcount(page) > INT_MAX/2)
        goto unlock;

This is defensive overflow protection.

Installing a page in another mapping increases mapcount/rmap state. If count is
already absurdly high, the kernel avoids further operations that could overflow
accounting.

This is a robustness guard, not the normal path.



SECTION 14: install_page() - RSS ACCOUNTING
================================================================================

This line is subtle:

    if (pte_none(*pte) || !zap_pte(mm, vma, addr, pte))
        inc_mm_counter(mm, file_rss);

Meaning:

    if target PTE was empty
    OR zap_pte() did NOT remove an existing normal page mapping

then increment file_rss

Why?

Because after install_page(), addr will contain one real file-backed resident
mapping.

If there was already some real file page there and zap_pte removed it, then RSS
already had a resident file page counted at that virtual slot and no net
increase is needed.

But if slot was empty or only held a non-present/file placeholder entry, we are
adding a new resident file mapping, so file_rss grows.



SECTION 15: install_page() - FINAL INSTALL
================================================================================

Core lines:

    flush_icache_page(vma, page);
    pte_val = mk_pte(page, prot);
    set_pte_at(mm, addr, pte, pte_val);
    page_add_file_rmap(page);
    update_mmu_cache(vma, addr, pte_val);
    lazy_mmu_prot_update(pte_val);

Meaning:

    flush instruction cache if needed
    build the real hardware PTE
    write it into page table
    add reverse mapping from page back to this VMA
    notify architecture-specific MMU side structures

So after this, user virtual address `addr` now directly maps `page`.



SECTION 16: install_file_pte()
================================================================================

Function:

    int install_file_pte(struct mm_struct *mm, struct vm_area_struct *vma,
                         unsigned long addr, unsigned long pgoff, pgprot_t prot)

Purpose:

    install not a real page, but a special encoded non-present PTE
    that says:

        "if this address faults later, map file page pgoff"

This is the key mechanism for nonlinear mappings.

Instead of eagerly bringing page cache pages in, the kernel can plant a marker
that remembers the file offset.



SECTION 17: install_file_pte() FLOW
================================================================================

Flow:

    get_locked_pte()
    if existing mapping had real page, zap it and adjust rss
    set_pte_at(mm, addr, pte, pgoff_to_pte(pgoff))
    unlock

This is cheaper than mapping a page immediately.

Later, a page fault handler can inspect this special file PTE, recover the
desired file offset, and populate the page.



SECTION 18: WHY install_file_pte() DOES NOT CALL update_mmu_cache()
================================================================================

The comment is important:

    this is not a real hardware-present PTE

It is a non-present entry like a swap entry.

So:

    no hardware TLB translation exists for it
    no normal MMU cache update is needed

Its purpose is only to preserve remapping metadata until fault time.



SECTION 19: WHAT IS A FILE PTE?
================================================================================

A "file PTE" here means:

    a PTE slot that is marked non-present
    but encodes file offset information

This is used in nonlinear VMAs because in a linear VMA, file offset can be
derived from:

    vm_pgoff + virtual offset within VMA

But in a nonlinear VMA that relationship is broken.

So the kernel must explicitly remember:

    this specific virtual page corresponds to file page X

and the file-PTE encoding stores exactly that.



SECTION 20: sys_remap_file_pages()
================================================================================

Function:

    asmlinkage long sys_remap_file_pages(...)

This is the syscall entry point.

Its job is to:

    validate arguments
    find the target VMA
    ensure the VMA supports this kind of remapping
    mark the VMA nonlinear if needed
    invoke populate() to install mappings/placeholders

This is the orchestrator.



SECTION 21: USER-LEVEL IDEA OF remap_file_pages()
================================================================================

The syscall conceptually says:

    "Inside this already-existing shared file mapping,
     remap the virtual range [start, start+size)
     so it refers to file pages beginning at pgoff."

Important detail:

    it does not create a new VMA
    it reuses an existing one

That is why it is efficient for "window remapping" patterns.



SECTION 22: ARGUMENT SANITIZATION
================================================================================

Early checks:

    if (__prot)
        return -EINVAL

Why?
    In this implementation, prot is ignored. Arbitrary per-remap protection is
    not supported here.

Then:

    start = start & PAGE_MASK;
    size  = size & PAGE_MASK;

This page-aligns the range.

Then:

    if (start + size <= start)
        return -EINVAL

This catches:

    wraparound
    zero-sized span after alignment

Then optionally:

    check pgoff fits in architecture's PTE file-encoding bit budget

This matters because file PTEs encode file offsets in PTE bits, so architecture
width may limit representable offsets.



SECTION 23: WHY PTE_FILE_MAX_BITS CHECK EXISTS
================================================================================

The file-PTE mechanism stores file offset in a PTE-like encoding.

Not all architectures/configurations have unlimited room for that.

So this check ensures:

    pgoff + number_of_pages
    fits within the number of bits available for file-PTE encoding

Otherwise the kernel could not represent the nonlinear mapping in page tables.



SECTION 24: LOCKING STRATEGY IN sys_remap_file_pages()
================================================================================

The function starts with:

    down_read(&mm->mmap_sem)

But may upgrade to write lock if needed.

Why start with read lock?

Because simple prefaulting/population within an already-compatible VMA does not
necessarily need VMA metadata changes.

Why need write lock sometimes?

Because if mapping becomes nonlinear, kernel must change:

    vma->vm_flags |= VM_NONLINEAR
    remove from linear i_mmap prio tree
    insert into nonlinear list

That changes VMA metadata and mapping structures, requiring write-side mmap_sem.



SECTION 25: FINDING AND VALIDATING THE VMA
================================================================================

Core validation:

    vma = find_vma(mm, start);

Then check:

    vma exists
    VM_SHARED is set
    vm_private_data rule is okay
    vma->vm_ops exists
    vma->vm_ops->populate exists
    requested range lies fully within this one VMA

This enforces that remap_file_pages works only for suitable mappings.

Why shared?

Because the feature is about remapping shared backing store pages in a stable,
file-backed fashion. It is not meant for arbitrary anonymous/private mappings in
this implementation.



SECTION 26: vm_private_data / VM_NONLINEAR CHECK
================================================================================

Condition:

    (!vma->vm_private_data || (vma->vm_flags & VM_NONLINEAR))

Comment says:

    vm_private_data is used as a swapout cursor in a VM_NONLINEAR vma

Meaning:

    if vm_private_data is already in use and VMA is not nonlinear,
    then the VMA is not in the expected state for this mechanism

This is a safety/consistency condition to avoid conflicting internal uses of
VMA-private metadata.



SECTION 27: LINEAR VS NONLINEAR DECISION
================================================================================

Key test:

    if (pgoff != linear_page_index(vma, start) &&
        !(vma->vm_flags & VM_NONLINEAR))

Meaning:

    if requested file offset differs from what a normal linear mapping would
    imply for this virtual address,
    then this mapping is no longer linear.

So kernel must mark:

    VM_NONLINEAR

If pgoff matches normal linear offset, then remap_file_pages may just behave
like explicit prefaulting of ordinary linear mapping.



SECTION 28: WHY VM_NONLINEAR MATTERS
================================================================================

In a normal linear VMA:

    fault handler can derive file offset from:
        vm_pgoff + (address - vm_start)/PAGE_SIZE

In a nonlinear VMA:

    that is no longer correct

So fault handlers, unmapping, truncation handling, swapout logic, etc. must know
this VMA has special semantics.

That is exactly what VM_NONLINEAR signals.

It tells the rest of MM:

    "do not assume virtual offset == file offset"



SECTION 29: RECLASSIFYING THE VMA IN MAPPING STRUCTURES
================================================================================

When converting to nonlinear:

    mapping = vma->vm_file->f_mapping;
    spin_lock(&mapping->i_mmap_lock);
    flush_dcache_mmap_lock(mapping);
    vma->vm_flags |= VM_NONLINEAR;
    vma_prio_tree_remove(vma, &mapping->i_mmap);
    vma_nonlinear_insert(vma, &mapping->i_mmap_nonlinear);
    flush_dcache_mmap_unlock(mapping);
    spin_unlock(&mapping->i_mmap_lock);

Meaning:

Before:
    VMA lived in normal file mapping prio tree

After:
    VMA lives in nonlinear VMA list/tree structure

Why?

Because linear truncation/invalidation logic can search by file offset ranges in
the prio tree. For nonlinear VMAs, that simple relation no longer holds, so they
must be tracked separately.



SECTION 30: WHY DCACHE/MMAP FLUSH LOCK HELPERS APPEAR
================================================================================

The flush_dcache_mmap_lock/unlock calls are architecture-sensitive helpers used
when changing file mapping visibility relationships.

Conceptually they help maintain consistency for architectures where aliasing
dcache issues or mapping-tracking interactions matter.

You can think of them as:

    "safely update file-backed VMA mapping structures"



SECTION 31: THE populate() CALLBACK
================================================================================

Finally:

    err = vma->vm_ops->populate(vma, start, size,
                                vma->vm_page_prot,
                                pgoff, flags & MAP_NONBLOCK);

This is where the actual page-table work happens.

Important:

    fremap.c itself does not know filesystem-specific page acquisition details
    it delegates to the VMA/file mapping implementation

populate() may:

    install real pages with install_page()
    install file-PTE placeholders with install_file_pte()
    perform readahead or fault-like prefaulting
    honor nonblocking mode

So this syscall is a generic coordinator, not the whole population engine.



SECTION 32: MAP_NONBLOCK
================================================================================

Flag handling:

    flags & MAP_NONBLOCK

This says roughly:

    do not block on I/O if possible

So populate() may choose not to synchronously bring in pages from disk.

In such cases it may install file-PTE placeholders rather than real pages, so
faults later complete the work on demand.



SECTION 33: WHY VM_NONLINEAR IS NOT CLEARED AGAIN
================================================================================

Comment says:

    We can't clear VM_NONLINEAR because we'd have to do it after populate
    completes, and that would prevent downgrading the lock. Locks can't be
    upgraded.

Meaning:

Once VMA has been converted to nonlinear during this operation, it stays so.

Even if a later remap happens to be linear-looking again, kernel does not try to
reclassify it back immediately.

This simplifies locking and avoids messy transitions.



SECTION 34: BIG PICTURE DATA FLOW
================================================================================

Normal linear file mapping:

    VMA says:
        vm_start, vm_end, vm_pgoff
    Fault path derives file offset from virtual offset

Nonlinear remapped mapping:

    sys_remap_file_pages()
        validates VMA
        maybe marks VM_NONLINEAR
        populate(start,size,pgoff,...)

populate may then do one of:

    A) install_page()
         addr -> real page now

    B) install_file_pte()
         addr -> encoded file offset placeholder

Later fault on B:
    file PTE decoded
    correct page loaded and installed



SECTION 35: ASCII FLOW
================================================================================

User process
    |
    | remap_file_pages(start, size, prot, pgoff, flags)
    v
sys_remap_file_pages()
    |
    |-- align + validate args
    |-- locate containing VMA
    |-- ensure:
    |      VM_SHARED
    |      vm_ops->populate exists
    |      range fully inside one VMA
    |
    |-- if requested pgoff != normal linear pgoff:
    |      acquire write semantics if needed
    |      mark VMA VM_NONLINEAR
    |      move VMA from mapping->i_mmap
    |      to mapping->i_mmap_nonlinear
    |
    |-- call vma->vm_ops->populate(...)
    |
    +--> populate path may:
            |
            |-- install_page()
            |      zap old pte
            |      build real pte
            |      add file rmap
            |
            `-- install_file_pte()
                   zap old pte
                   encode pgoff in nonpresent pte
                   fault later resolves it



SECTION 36: RELATION TO filemap.c / memory.c
================================================================================

This file is not standalone.

It works with other MM pieces:

    filemap.c
        provides populate logic for file-backed VMAs

    memory.c
        page fault machinery that interprets file PTEs in nonlinear VMAs

    mmap.c
        VMA creation and general mmap infrastructure

So fremap.c is one layer in a larger design:

    mmap layer
        +
    nonlinear file-PTE encoding
        +
    file fault/populate layer
        +
    page table install/remove helpers



SECTION 37: KEY INSIGHT ABOUT NONLINEAR VMAS
================================================================================

A normal file VMA only needs:

    one base file offset (vm_pgoff)

A nonlinear VMA may need:

    per-page file offsets

That is why the kernel sometimes stores file offsets directly in PTEs.

This is the most important conceptual takeaway from this file.



SECTION 38: install_page() VS install_file_pte()
================================================================================

install_page():
    eager
    maps a real page right now
    contributes to resident file RSS
    needs rmap + MMU update

install_file_pte():
    lazy metadata encoding
    not resident yet
    no MMU update needed
    lets future page fault know which file page to bring in

So:

    install_page   = "materialize now"
    install_file_pte = "remember what to materialize later"



SECTION 39: WHY THIS APPROACH IS EFFICIENT
================================================================================

Compared to repeatedly destroying and recreating VMAs:

    cheaper metadata churn
    avoids many mmap/munmap operations
    keeps one shared mapping window
    page-table level retargeting is flexible

Especially useful for large-file applications that want a sliding or random
window into backing storage.



SECTION 40: SUMMARY
================================================================================

fremap.c provides the MM support for explicit page-table population and
nonlinear shared file mappings.

Main ideas:

    zap_pte()
        removes old mapping cleanly

    install_page()
        installs a real resident file page at a virtual address

    install_file_pte()
        installs a non-present file-offset placeholder PTE

    sys_remap_file_pages()
        validates an existing shared file mapping, marks it nonlinear if
        needed, and asks the VMA to populate page tables for arbitrary file
        offsets

In one sentence:

    this file lets a shared file-backed VMA stop being a simple linear
    virtual-to-file mapping and become a programmable per-page mapping window.


================================================================================
END OF FILE
================================================================================
