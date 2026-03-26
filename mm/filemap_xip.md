================================================================================
FILE: linux_filemap_xip_background_and_flow.txt
TOPIC: Execute-In-Place File I/O and mmap (mm/filemap_xip.c)
SOURCE: mm/filemap_xip.c
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHY filemap_xip.c EXISTS
================================================================================

Normal file I/O in Linux usually goes through the page cache:

    storage
      |
      v
   readpage()
      |
      v
   page cache page
      |
      v
   copy to user / map into process

But some storage types can support:

    EXECUTE IN PLACE (XIP)

Meaning:

    file data can be accessed directly from its storage-backed memory
    without first copying it into the page cache


This is useful for media like:

    NOR flash
    persistent memory-like mappings
    storage where direct page-aligned access is possible


So filemap_xip.c exists to provide the generic file read/write/mmap logic
for filesystems that implement:

    mapping->a_ops->get_xip_page()


Instead of asking the filesystem:

    "please fill a page-cache page with readpage()"

the XIP path asks:

    "give me the actual page that directly backs this file block"


That is the central difference.



SECTION 2: WHAT XIP CHANGES COMPARED TO NORMAL filemap.c
================================================================================

Normal cached file path:

    file offset
        |
        v
    page cache lookup
        |
        +--> if miss: allocate page-cache page
        +--> read data from disk into page-cache page
        |
        v
    copy/map page-cache page


XIP path:

    file offset
        |
        v
    get_xip_page(mapping, sector, create?)
        |
        v
    get directly addressable backing page
        |
        v
    copy/map that page directly


So XIP removes a whole layer:

    NO ordinary page-cache fill path
    NO readpage-based caching path
    NO page-cache insertion for file data

Instead it works with filesystem-provided backing pages directly.



SECTION 3: THE KEY CALLBACK: get_xip_page()
================================================================================

Everything in this file depends on:

    mapping->a_ops->get_xip_page(mapping, sector, create)

This callback is supplied by the filesystem.

Conceptually it means:

    "for this file block, give me the directly addressable page"


Parameters conceptually are:

    mapping
        which file / address_space

    sector
        which underlying block/sector region

    create
        0 = just lookup existing backing
        1 = allocate backing block/page if needed


Possible outcomes:

    returns valid struct page *
        success

    returns ERR_PTR(-ENODATA)
        sparse hole / no backing data

    returns other ERR_PTR(-Exxx)
        actual error

    returns NULL
        failure in some places treated as I/O error


This callback replaces much of what readpage() and page-cache population
would do in the ordinary cached path.



SECTION 4: BIG PICTURE OF THIS FILE
================================================================================

This source provides generic XIP support for:

    1) xip_file_read()
    2) xip_file_sendfile()
    3) xip_file_mmap()
    4) xip_file_write()
    5) xip_truncate_page()

And internally:

    do_xip_mapping_read()
    xip_file_nopage()
    __xip_unmap()
    __xip_file_write()


So this file is the XIP equivalent of a subset of normal filemap.c.


Mental model:

    filemap.c     = generic buffered/page-cache file I/O
    filemap_xip.c = generic direct-file-page XIP I/O



SECTION 5: WHY PAGE CACHE IS MOSTLY BYPASSED
================================================================================

In normal filemap.c, a file page is represented in page cache as:

    mapping->page_tree[index] -> struct page

and that page is populated by readpage().

In XIP:

    the filesystem already has directly usable backing pages

So for reads and mmap faults, the kernel does not need to:

    allocate page-cache page
    submit readpage I/O into that page
    wait for PageUptodate


Instead it can ask:

    "what page directly represents this file block?"

This can reduce memory copying and page-cache overhead.

But it also means:

    sparse holes
    write sharing
    mmap consistency
    zero-page replacement

must be handled carefully.



SECTION 6: XIP READ CORE
================================================================================

Main function:

    do_xip_mapping_read()

This is the XIP analogue of do_generic_mapping_read() from filemap.c.

Its job is:

    take a file position and read request
    walk file pages
    fetch XIP pages with get_xip_page()
    hand bytes from those pages to an actor


Important detail:

    struct file *filp is not actually used by the low-level lookup here
    it may be NULL


Unlike buffered reads, this function does not do page-cache lookup,
readpage(), or PageUptodate checks.

That is the main simplification.



SECTION 7: do_xip_mapping_read() STEP-BY-STEP
================================================================================

STEP 1: Validate XIP support

    BUG_ON(!mapping->a_ops->get_xip_page);

This path only works if filesystem provides get_xip_page().


STEP 2: Convert file position to page index and in-page offset

    index  = *ppos >> PAGE_CACHE_SHIFT
    offset = *ppos & ~PAGE_CACHE_MASK


STEP 3: Check file size

    isize = i_size_read(inode)

If zero:
    stop

Compute last valid file page:

    end_index = (isize - 1) >> PAGE_CACHE_SHIFT


STEP 4: Determine how many bytes to read from current page

Normally:

    nr = PAGE_CACHE_SIZE

But on last page of file:

    nr = valid bytes up to EOF

Then subtract current page offset.


STEP 5: Ask filesystem for XIP page

    page = mapping->a_ops->get_xip_page(mapping,
                index * (PAGE_SIZE / 512), 0);

Important detail:

    this code converts page index into sector units
    using PAGE_SIZE / 512


Meaning:

    page 0 -> sector 0
    page 1 -> sector PAGE_SIZE/512
    etc.


STEP 6: Handle result

Case A: valid page returned
    good, use it

Case B: ERR_PTR(-ENODATA)
    sparse hole
    use ZERO_PAGE(0)

Case C: other error
    desc->error = PTR_ERR(page)
    stop

Case D: NULL
    treated as I/O failure in this function
    goto no_xip_page -> -EIO


STEP 7: Cache alias safety

If mapping is writable-mapped:

    flush_dcache_page(page)

This is the same kind of precaution seen in normal filemap code.


STEP 8: Copy bytes using actor

    ret = actor(desc, page, offset, nr)

Actor may be:

    file_read_actor()  for normal reads
    file_send_actor()  for sendfile-like behavior


STEP 9: Advance file position

    offset += ret
    index += offset >> PAGE_CACHE_SHIFT
    offset &= ~PAGE_CACHE_MASK

If more bytes remain:
    continue


This loop repeats until:
    request satisfied
    EOF
    error



SECTION 8: HOW SPARSE HOLES ARE HANDLED ON READ
================================================================================

One important XIP detail is sparse files.

If get_xip_page() returns:

    ERR_PTR(-ENODATA)

that means:

    this logical file range has no actual backing block/page allocated

For reads, the code treats this as a hole and uses:

    ZERO_PAGE(0)

So reading a sparse hole behaves like normal filesystem semantics:

    you read zero bytes (conceptually zero-filled data)

This is a clever way to preserve sparse-file behavior without needing page cache.



SECTION 9: xip_file_read()
================================================================================

Purpose:

    implement normal read() for XIP files


Flow:

    1) verify user buffer writable with access_ok()
    2) initialize read_descriptor_t
    3) call do_xip_mapping_read(..., file_read_actor)
    4) return desc.written if any bytes copied
       else return desc.error


So this is essentially:

    front-end validation + generic XIP read core


It mirrors the normal file read structure, but the inner engine is XIP-specific.



SECTION 10: xip_file_sendfile()
================================================================================

Purpose:

    implement sendfile-like reading for XIP files


Flow is the same as xip_file_read(), except:

    actor is caller-supplied
    desc.arg.data = target

So, again, the design is:

    one generic read traversal
    multiple consumers of the bytes


This mirrors the actor-based design from normal filemap.c.



SECTION 11: WHY __xip_unmap() EXISTS
================================================================================

Function:

    __xip_unmap(mapping, pgoff)

This is one of the most interesting parts of the file.

Purpose:

    walk all VMAs mapping this file
    find mappings at this page offset
    unmap ZERO_PAGE mappings there


Why is this needed?

Because with XIP and sparse files, a hole may initially be mapped as:

    ZERO_PAGE(0)

Later, if someone writes to that sparse block and real backing storage is
allocated, old VMAs may still map the shared zero page.

Those mappings must be invalidated so future faults or accesses use the new
real backing page instead of stale zero-page mappings.

So __xip_unmap() is the coherence fixup when sparse holes become real blocks.



SECTION 12: __xip_unmap() STEP-BY-STEP
================================================================================

STEP 1: Lock the mapping's i_mmap tree

    spin_lock(&mapping->i_mmap_lock);

This protects the VMA tree for the mapping.


STEP 2: Iterate all VMAs covering this page offset

    vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, pgoff, pgoff)

So it finds all VMAs mapping this file page.


STEP 3: Compute faulting virtual address inside each VMA

    address = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT)


STEP 4: Look up page table entry for ZERO_PAGE

    page = ZERO_PAGE(0)
    pte = page_check_address(page, mm, address, &ptl)

If a PTE exists and maps that zero page:

    it is the stale placeholder mapping we want to remove


STEP 5: Unmap it

    flush_cache_page(vma, address, pte_pfn(*pte));
    pteval = ptep_clear_flush(vma, address, pte);
    page_remove_rmap(page, vma);
    dec_mm_counter(mm, file_rss);
    BUG_ON(pte_dirty(pteval));
    pte_unmap_unlock(pte, ptl);
    page_cache_release(page);


Meaning:

    remove the PTE
    update reverse mapping/accounting
    release page reference


After this, next access will fault again and obtain the real page.


This function is very much about:

    keeping mmap views coherent when sparse holes transition to real blocks.



SECTION 13: XIP mmap PAGE FAULT HANDLING
================================================================================

Main function:

    xip_file_nopage()

This is the XIP analogue of filemap_nopage().

It is called when a process faults on a file-backed XIP mapping.


Overall flow:

    1) compute file page offset from fault address
    2) ensure it is within file size
    3) call get_xip_page(..., create=0)
    4) if valid page, return it
    5) if sparse hole (-ENODATA):
           if shared writable mapping on writable fs:
               allocate real block with create=1
               unmap zero-page from other VMAs
           else:
               use ZERO_PAGE(0)
    6) get ref on page and return it


So XIP mmap faults do not populate page cache.
They directly return the filesystem's XIP page or zero page.



SECTION 14: xip_file_nopage() STEP-BY-STEP
================================================================================

STEP 1: Compute pgoff

    pgoff = ((address - area->vm_start) >> PAGE_CACHE_SHIFT) + area->vm_pgoff

Standard file-backed VMA logic.


STEP 2: Check file size

    size = (i_size_read(inode) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT

If pgoff >= size:
    return NULL

This indicates fault beyond file content.


STEP 3: Try lookup without creation

    page = get_xip_page(mapping, pgoff*(PAGE_SIZE/512), 0)

If valid page:
    goto out


STEP 4: Handle error

If error is not -ENODATA:
    return NULL

Meaning:
    only sparse-hole case gets special handling


STEP 5: Sparse-hole mmap case

If VMA is shared+writable and filesystem is writable:

    page = get_xip_page(mapping, pgoff*(PAGE_SIZE/512), 1)

This allocates real backing for the sparse hole.

If that succeeds:

    __xip_unmap(mapping, pgoff)

to remove stale ZERO_PAGE mappings from other VMAs.


Otherwise, for nonshared or readonly-ish situations:

    page = ZERO_PAGE(0)


STEP 6: Return referenced page

    page_cache_get(page);
    return page;


Important note:

    even though this is not a normal page-cache page path,
    the code still takes a page reference before returning.



SECTION 15: WHY SHARED WRITABLE MAPPINGS ARE SPECIAL
================================================================================

Suppose a sparse file hole is mmap'd shared and writable.

If a process writes there, POSIX-like behavior expects:

    the file gains real storage
    the shared mapping reflects actual stored data
    other sharers see the real page, not a forever-zero placeholder

So the code checks:

    (VM_WRITE | VM_MAYWRITE)
    and
    (VM_SHARED | VM_MAYSHARE)
    and
    filesystem not MS_RDONLY

In that case, a real block must be allocated.

Otherwise, readonly/private-style access can just use ZERO_PAGE safely.



SECTION 16: xip_file_mmap()
================================================================================

Purpose:

    install XIP-specific VMA operations


Flow:

    BUG_ON(!get_xip_page)
    file_accessed(file)
    vma->vm_ops = &xip_file_vm_ops

where:

    xip_file_vm_ops = {
        .nopage = xip_file_nopage,
    }


So any future page fault on this VMA uses XIP fault handling instead of
normal page-cache-based file faults.



SECTION 17: XIP WRITE CORE
================================================================================

Main function:

    __xip_file_write()

This is the write-side analogue for XIP files.

Instead of:

    grab page-cache page
    prepare_write()
    copy into page cache
    commit_write()

it does:

    get_xip_page() for actual backing page
    allocate backing if sparse
    maybe unmap ZERO_PAGE from sharers
    copy user bytes directly into backing page


So XIP write goes straight to the underlying directly mapped storage page.



SECTION 18: __xip_file_write() STEP-BY-STEP
================================================================================

STEP 1: Require XIP support

    BUG_ON(!mapping->a_ops->get_xip_page);


STEP 2: Loop over pages covered by the write

For each iteration:

    offset = pos & (PAGE_CACHE_SIZE - 1)
    index  = pos >> PAGE_CACHE_SHIFT
    bytes  = PAGE_CACHE_SIZE - offset
    bytes  = min(bytes, count)


STEP 3: Fault in user source pages first

    fault_in_pages_readable(buf, bytes)

Same rationale as ordinary buffered writes:
avoid deadlocks/fault recursion while holding file/page state.


STEP 4: Get XIP backing page

    page = get_xip_page(mapping, index*(PAGE_SIZE/512), 0)

If sparse hole (-ENODATA):
    allocate real backing:

        page = get_xip_page(mapping, index*(PAGE_SIZE/512), 1)

If allocation succeeds:
    __xip_unmap(mapping, index)

So any stale ZERO_PAGE mmap is removed.


STEP 5: Copy bytes from user into backing page

    copied = filemap_copy_from_user(page, offset, buf, bytes)

Then:

    flush_dcache_page(page)


STEP 6: Advance counters

If copy succeeded:
    written += status
    count   -= status
    pos     += status
    buf     += status


STEP 7: On exit, update file size if extended

    if (pos > inode->i_size) {
        i_size_write(inode, pos);
        mark_inode_dirty(inode);
    }


So this is direct modification of the storage-backed XIP page,
not dirty-page-cache writeback logic.



SECTION 19: xip_file_write()
================================================================================

Purpose:

    public front-end for XIP writes


Flow:

    mutex_lock(&inode->i_mutex)

    access_ok(VERIFY_READ, buf, len)

    pos   = *ppos
    count = len

    vfs_check_frozen(inode->i_sb, SB_FREEZE_WRITE)

    current->backing_dev_info = mapping->backing_dev_info

    ret = generic_write_checks(...)
    ret = remove_suid(...)
    file_update_time(filp)

    ret = __xip_file_write(...)

    current->backing_dev_info = NULL

    mutex_unlock(&inode->i_mutex)

    return ret


This front-end closely mirrors the generic write front-end in filemap.c.

It still uses generic infrastructure for:

    write-limit checks
    suid/sgid stripping
    inode timestamps
    sb freeze checks
    backing_dev_info association


But the actual data path is XIP-specific.



SECTION 20: WHY current->backing_dev_info IS SET HERE
================================================================================

As in normal write paths, the code sets:

    current->backing_dev_info = mapping->backing_dev_info

This lets reclaim/writeback-related code know which backing device context
the current task is associated with.

Even though XIP bypasses normal page-cache writeback for data movement,
the broader VM/accounting/reclaim ecosystem still wants this context.



SECTION 21: XIP TRUNCATE SUPPORT
================================================================================

Function:

    xip_truncate_page(mapping, from)

Purpose:

    zero the tail of a partial block/page when truncating a file


This is analogous to block_truncate_page(), but for XIP.


Why needed?

If truncation cuts a file in the middle of a block, bytes past new EOF in the
same block must be zeroed so stale old data is not exposed later.


Since XIP does not use ordinary page-cache lookup here, it uses:

    get_xip_page()

to obtain the actual backing page directly.



SECTION 22: xip_truncate_page() STEP-BY-STEP
================================================================================

STEP 1: Convert offset to page index and in-page offset

    index  = from >> PAGE_CACHE_SHIFT
    offset = from & (PAGE_CACHE_SIZE - 1)


STEP 2: Determine filesystem block size

    blocksize = 1 << mapping->host->i_blkbits

Compute how far into the block the truncate point is:

    length = offset & (blocksize - 1)


STEP 3: If already on block boundary, nothing to do

    if (!length)
        return 0


STEP 4: Compute bytes to zero in the block

    length = blocksize - length


STEP 5: Fetch XIP page

    page = get_xip_page(mapping, index*(PAGE_SIZE/512), 0)

Cases:

    no page -> -ENOMEM
    -ENODATA -> hole, nothing to zero
    other error -> return error


STEP 6: Zero the tail bytes

    kaddr = kmap_atomic(page, KM_USER0);
    memset(kaddr + offset, 0, length);
    kunmap_atomic(kaddr, KM_USER0);

    flush_dcache_page(page)


This preserves correct truncate semantics.



SECTION 23: WHAT XIP DOES NOT NEED FROM NORMAL filemap.c
================================================================================

Compared to the normal buffered page-cache path, XIP avoids much of:

    add_to_page_cache()
    find_get_page(mapping, index)
    PageUptodate state management
    readpage() into page-cache page
    dirty-page writeback of cached data
    invalidate cache pages after direct access


Why?

Because the backing pages themselves are the authoritative data source.


This is the whole point of execute-in-place:
remove the indirection through the page cache for file contents.



SECTION 24: BUT XIP INTRODUCES DIFFERENT COMPLEXITIES
================================================================================

Although XIP removes page-cache copying overhead, it introduces special issues:

    1) sparse holes must map to ZERO_PAGE until real storage exists
    2) shared writable mappings must transition from ZERO_PAGE to real backing
    3) VMAs must be unmapped when zero-page placeholders become real pages
    4) filesystem must provide directly addressable backing pages
    5) truncate must zero partial blocks directly


So XIP trades:

    page-cache complexity
for
    direct-backing-page coherence complexity



SECTION 25: COMPLETE XIP READ FLOW DIAGRAM
================================================================================

User read()
    |
    v
xip_file_read()
    |
    +--> access_ok(user buffer)
    +--> initialize read_descriptor
    +--> do_xip_mapping_read()
            |
            +--> compute file page index + offset
            +--> check EOF
            +--> get_xip_page(mapping, sector, create=0)
                    |
                    +--> valid page:
                    |       actor copies bytes from page
                    |
                    +--> -ENODATA:
                    |       use ZERO_PAGE
                    |       actor copies zeroes
                    |
                    +--> other error:
                            desc->error = error
            |
            +--> advance ppos
    |
    v
return bytes or error



SECTION 26: COMPLETE XIP mmap FAULT FLOW DIAGRAM
================================================================================

Process faults on XIP-mapped file address
    |
    v
xip_file_nopage()
    |
    +--> compute pgoff from address
    +--> check within file size
    +--> get_xip_page(mapping, sector, create=0)
            |
            +--> valid page:
            |       page_cache_get(page)
            |       return page
            |
            +--> -ENODATA:
                    |
                    +--> shared writable + fs writable?
                    |       |
                    |       +--> get_xip_page(..., create=1)
                    |       +--> __xip_unmap(mapping, pgoff)
                    |       +--> return real page
                    |
                    +--> otherwise:
                            return ZERO_PAGE with ref
    |
    v
fault handler installs returned page into PTE



SECTION 27: COMPLETE XIP WRITE FLOW DIAGRAM
================================================================================

User write()
    |
    v
xip_file_write()
    |
    +--> lock inode->i_mutex
    +--> access_ok(source buffer)
    +--> generic_write_checks()
    +--> remove_suid()
    +--> file_update_time()
    +--> __xip_file_write()
            |
            +--> for each covered page:
                    |
                    +--> fault_in_pages_readable(user buf)
                    +--> get_xip_page(..., create=0)
                            |
                            +--> valid page:
                            |       copy user bytes -> page
                            |
                            +--> -ENODATA:
                                    get_xip_page(..., create=1)
                                    __xip_unmap(mapping, index)
                                    copy user bytes -> new page
                    |
                    +--> flush_dcache_page(page)
                    +--> advance pos/count
            |
            +--> if file grew:
                    i_size_write()
                    mark_inode_dirty()
    |
    +--> clear current->backing_dev_info
    +--> unlock inode->i_mutex
    |
    v
return bytes or error



SECTION 28: RELATION TO NORMAL filemap.c
================================================================================

Normal filemap.c read path:

    page cache lookup
    maybe readpage()
    PageUptodate
    copy to user


filemap_xip.c read path:

    get_xip_page()
    maybe ZERO_PAGE for sparse
    copy to user


Normal mmap fault:

    filemap_nopage()
    page cache / readpage machinery


XIP mmap fault:

    xip_file_nopage()
    direct filesystem backing page


Normal buffered write:

    prepare_write()
    copy into page cache
    commit_write()
    later writeback


XIP write:

    get_xip_page()
    allocate real block if sparse
    copy directly into backing page


So filemap_xip.c is not just a small variation.
It is a different data path model built around direct backing pages.



SECTION 29: SIMPLE MENTAL MODEL
================================================================================

Think of normal file I/O like this:

    warehouse (storage) -> staging shelf (page cache) -> customer

XIP is like:

    warehouse shelf is directly accessible to the customer

No staging shelf is needed.

But when an empty shelf location (a sparse hole) is accessed:

    readonly visitor:
        show them a dummy empty display (ZERO_PAGE)

    shared writer:
        build a real shelf there first,
        remove old dummy displays from everyone else,
        then let them write to the real shelf


That is exactly what the ZERO_PAGE and __xip_unmap logic are doing.



SECTION 30: SUMMARY
================================================================================

mm/filemap_xip.c implements generic execute-in-place file support.

Main idea:

    access filesystem-backed pages directly through get_xip_page()
    instead of going through ordinary page-cache readpage/writeback paths


Key responsibilities:

    xip_file_read()
        read from directly mapped backing pages

    xip_file_sendfile()
        actor-based sendfile-style reads from XIP pages

    xip_file_nopage()
        serve mmap faults for XIP mappings

    __xip_unmap()
        remove stale ZERO_PAGE mappings when sparse holes become real blocks

    xip_file_write()
        copy user data directly into XIP backing pages

    xip_truncate_page()
        zero partial block tails on truncate


Key special behaviors:

    sparse holes read as ZERO_PAGE
    shared writable sparse faults allocate real backing
    old zero-page mappings are explicitly unmapped
    file size is updated directly after XIP writes


In short:

    filemap_xip.c is the generic Linux layer for filesystems whose file data
    can be executed or accessed directly in place, without the normal page
    cache copy/fill path.


================================================================================
END OF FILE
================================================================================

