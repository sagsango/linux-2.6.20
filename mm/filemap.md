================================================================================
FILE: linux_filemap_background_and_flow.txt
TOPIC: Generic File Page Cache, Buffered I/O, mmap Faults, and Writeback Glue
SOURCE: mm/filemap.c
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHY filemap.c EXISTS
================================================================================

This file is one of the core files of the Linux page cache.

It provides the generic machinery used by "normal" filesystems for:

    1) buffered file reads
    2) buffered file writes
    3) page cache insertion / lookup
    4) mmap file page faults
    5) writeback start / wait helpers
    6) direct I/O coordination with page cache
    7) page lock waiting / wakeup
    8) page cache invalidation and removal helpers

Think of filemap.c as the bridge between:

    VFS / sys_read / sys_write / mmap
and
    page cache / address_space / filesystem a_ops


It does NOT know filesystem-specific disk layout.

Instead it relies on filesystem callbacks such as:

    mapping->a_ops->readpage
    mapping->a_ops->writepage
    mapping->a_ops->prepare_write
    mapping->a_ops->commit_write
    mapping->a_ops->direct_IO
    mapping->a_ops->releasepage

So the generic logic is here, while actual filesystem/block details live below.



SECTION 2: THE BIG PICTURE
================================================================================

For a cached file, the main path is:

    user read()
        |
        v
    generic_file_aio_read()
        |
        v
    do_generic_mapping_read()
        |
        +--> find page in page cache
        |       |
        |       +--> if present and uptodate: copy to user
        |       |
        |       +--> if missing: allocate page, add to cache, readpage()
        |
        v
    copy bytes to user


For a buffered write:

    user write()
        |
        v
    generic_file_aio_write()
        |
        v
    generic_file_buffered_write()
        |
        +--> grab/create page cache page
        +--> prepare_write()
        +--> copy user bytes into page cache page
        +--> commit_write()
        +--> dirty page/writeback later


For mmap page fault:

    process accesses mapped file address
        |
        v
    filemap_nopage()
        |
        +--> find page in page cache
        +--> if missing, page_cache_read()
        +--> ensure PageUptodate
        +--> return page to fault handler


So filemap.c is the "generic page cache side" of file I/O.



SECTION 3: IMPORTANT CORE OBJECTS
================================================================================

struct file

    open file instance


struct inode

    persistent file metadata


struct address_space

    page-cache container for the file


struct page

    one cached page of file data


mapping->page_tree

    radix tree indexing cached pages by page index


mapping->tree_lock

    protects page_tree and related state


mapping->a_ops

    address_space operations supplied by filesystem


file->f_ra

    per-file readahead state


The key relationship is:

    inode
      |
      +--> address_space mapping
              |
              +--> radix tree of struct page objects

So file data in cache is stored as pages indexed by file offset/PAGE_SIZE.



SECTION 4: PAGE CACHE AS THE CENTRAL IDEA
================================================================================

A file is not read from disk for every read() call.

Instead the kernel keeps file contents in memory:

    file offset 0..4095     -> page index 0
    file offset 4096..8191  -> page index 1
    file offset 8192..12287 -> page index 2
    ...

These cached pages are stored in the mapping radix tree.

So a read path usually does:

    "find page in mapping->page_tree"

If found and uptodate:

    no disk I/O needed

If not found:

    allocate page
    insert into page cache
    ask filesystem to fill it via readpage()


That is the core model behind most of this file.



SECTION 5: LOCKING BACKGROUND
================================================================================

At the top of the file you see a long lock ordering comment.

That comment is extremely important because filemap.c sits at the
intersection of:

    page cache
    page tables
    writeback
    truncate
    mmap
    reclaim
    swap
    buffer heads

These subsystems all interact with the same pages.

So lock ordering prevents deadlocks.

A few especially important locks here are:

    mapping->tree_lock
        protects radix tree page cache membership

    page lock (PG_locked)
        serializes I/O/fill/truncate against a specific page

    inode->i_mutex
        serializes many file data/size changing operations

    mmap_sem / i_mmap_lock
        relate file-backed VMAs and page faults


You can think of filemap.c as a place where correct locking is more important
than raw algorithmic complexity.



SECTION 6: REMOVING A PAGE FROM PAGE CACHE
================================================================================

Functions:

    __remove_from_page_cache()
    remove_from_page_cache()

Purpose:

    detach a page from its mapping radix tree


Core steps:

    radix_tree_delete(&mapping->page_tree, page->index);
    page->mapping = NULL;
    mapping->nrpages--;
    __dec_zone_page_state(page, NR_FILE_PAGES);


Meaning:

    the page is no longer a cached file page for that mapping


Important:

    caller must already ensure page is safe to remove
    page must be locked
    tree_lock must protect radix tree modification


This happens in truncate, invalidation, reclaim, and similar paths.



SECTION 7: PAGE WAITING INFRASTRUCTURE
================================================================================

Functions and helpers:

    page_waitqueue()
    wait_on_page_bit()
    unlock_page()
    end_page_writeback()
    __lock_page()
    __lock_page_nosync()

Pages do not each carry a full waitqueue object.
That would be expensive.

Instead Linux hashes pages to waitqueue buckets:

    zone->wait_table[hash(page)]

So multiple pages may share one waitqueue bucket.

Waiters sleep on a hashed queue and recheck the page bit they care about.

Bits commonly waited on:

    PG_locked
    PG_writeback

This is space-efficient, but may cause "thundering herd" wakeups when
hash collisions happen.


Mental model:

    wait_on_page_bit(page, PG_locked)
        sleep until that page's lock bit clears

    unlock_page(page)
        clear PG_locked and wake waiters

    end_page_writeback(page)
        clear PG_writeback and wake waiters


This shared wait-bit design is foundational for page-cache I/O synchronization.



SECTION 8: WHY sync_page() EXISTS IN PAGE WAITING
================================================================================

Function:

    sync_page(void *word)

This helper is used while waiting on a page bit.

It tries to help progress stalled I/O by calling:

    mapping->a_ops->sync_page(page)

if available, then:

    io_schedule()


The idea is:

    "while waiting for this page's I/O-related state to clear,
     let the backing device / block layer make progress"

This is especially useful when pages are under writeback or waiting for I/O.



SECTION 9: WRITING DIRTY PAGE CACHE PAGES
================================================================================

Functions:

    __filemap_fdatawrite_range()
    filemap_fdatawrite()
    filemap_flush()
    filemap_write_and_wait()
    filemap_write_and_wait_range()
    filemap_fdatawait()
    wait_on_page_writeback_range()

These are generic helpers for starting writeback and waiting for it.

Key idea:

    page cache may contain dirty file pages
    these must be written back to storage


The actual filesystem submission work happens through:

    do_writepages(mapping, &wbc)

where wbc is:

    struct writeback_control


This file prepares the control structure and coordinates waiting.
It does not itself emit block requests directly.



SECTION 10: __filemap_fdatawrite_range()
================================================================================

Purpose:

    start writeback of dirty pages in a byte range


Flow:

    initialize writeback_control:
        sync_mode
        nr_to_write
        range_start
        range_end

    if mapping cannot writeback dirty pages:
        return 0

    call do_writepages(mapping, &wbc)


Important sync modes:

    WB_SYNC_NONE
        more opportunistic / mostly nonblocking flush

    WB_SYNC_ALL
        full data-integrity style writeback


So this function says:

    "filesystem, please write dirty pages in this range"



SECTION 11: filemap_flush() vs filemap_fdatawrite()
================================================================================

filemap_flush(mapping)

    uses WB_SYNC_NONE
    mostly nonblocking
    not strong enough for integrity guarantees


filemap_fdatawrite(mapping)

    uses WB_SYNC_ALL
    stronger data-integrity behavior


This distinction matters:

    flush = start writeback
    write_and_wait = start writeback and wait for completion/errors



SECTION 12: WAITING FOR WRITEBACK COMPLETION
================================================================================

Function:

    wait_on_page_writeback_range(mapping, start, end)

Purpose:

    walk tagged WRITEBACK pages in the mapping and wait for them


Mechanism:

    pagevec_lookup_tag(..., PAGECACHE_TAG_WRITEBACK, ...)
        finds pages in radix tree tagged as under writeback

Then for each such page:

    wait_on_page_writeback(page)

If PageError(page) is set:

    ret = -EIO


At the end it also checks mapping-wide error flags:

    AS_ENOSPC
    AS_EIO

and returns those as appropriate.


So this is the generic:

    "wait until all file pages in range stop being written back"



SECTION 13: sync_page_range() / sync_page_range_nolock()
================================================================================

Purpose:

    write and wait on a file byte range for O_SYNC / fsync-like needs


Flow:

    1) filemap_fdatawrite_range(mapping, pos, pos+count-1)
    2) generic_osync_inode(inode, mapping, OSYNC_METADATA)
    3) wait_on_page_writeback_range(...)


Difference:

    sync_page_range()
        takes inode->i_mutex around generic_osync_inode()

    sync_page_range_nolock()
        does not


Why?

Holding i_mutex too long can serialize unrelated writers to different parts
of the same file, so the nolock version exists for callers that manage
serialization differently.



SECTION 14: ADDING A PAGE TO PAGE CACHE
================================================================================

Functions:

    add_to_page_cache()
    add_to_page_cache_lru()

Purpose:

    insert a page into mapping->page_tree under a given file index


Flow:

    radix_tree_preload(...)
    write_lock_irq(&mapping->tree_lock)
    radix_tree_insert(...)
    if success:
        page_cache_get(page)
        SetPageLocked(page)
        page->mapping = mapping
        page->index = offset
        mapping->nrpages++
        __inc_zone_page_state(page, NR_FILE_PAGES)
    unlock
    end preload

Then add_to_page_cache_lru() additionally does:

    lru_cache_add(page)


Meaning:

    the page is now:
        in page cache
        locked
        counted as file page
        visible in radix tree
        eligible for reclaim via LRU


This is one of the most central transitions in the whole file.



SECTION 15: FINDING PAGES IN PAGE CACHE
================================================================================

Functions:

    find_get_page()
    find_trylock_page()
    find_lock_page()
    find_or_create_page()
    find_get_pages()
    find_get_pages_contig()
    find_get_pages_tag()

These are the generic lookup helpers over mapping->page_tree.


Examples:

find_get_page(mapping, index)

    look up page by index
    if found, increment refcount
    return page


find_lock_page(mapping, index)

    look up page
    get ref
    if already locked, sleep until it unlocks
    revalidate mapping/index after sleep


find_or_create_page(mapping, index, gfp)

    try lookup
    if missing allocate page
    insert into cache
    return locked page


Gang lookups:

    find_get_pages()
    find_get_pages_contig()
    find_get_pages_tag()

These fetch batches of pages, useful for readahead, writeback, invalidate,
and scans.


These helpers are the basic "page-cache namespace API."



SECTION 16: WHY find_lock_page() REVALIDATES AFTER SLEEP
================================================================================

In find_lock_page():

    page = radix_tree_lookup(...)
    page_cache_get(page)
    if locked:
        sleep
        re-acquire tree lock
        check:
            page->mapping == mapping
            page->index == offset

Why?

Because while sleeping, the page may have been:

    truncated
    removed
    recycled
    moved out of this mapping


So after waking, the old page pointer must be revalidated before use.

This is a common pattern in page-cache code:
sleep first, then re-check object identity/state.



SECTION 17: GENERIC BUFFERED READ CORE
================================================================================

Main function:

    do_generic_mapping_read()

This is the heart of buffered file reading through the page cache.

Inputs:

    mapping   = file's page cache
    _ra       = file readahead state
    filp      = file
    *ppos     = file position
    desc      = read descriptor
    actor     = function that consumes bytes from pages


The actor abstraction lets the same core logic support:

    normal read() to user buffer
    sendfile()
    other page-to-consumer operations


Overall flow:

    1) compute page index and offset from file position
    2) maybe trigger readahead
    3) look up page in page cache
    4) if missing, allocate/add page and call readpage()
    5) if page exists but not uptodate, lock and fill it
    6) once page is uptodate, actor copies bytes out
    7) advance file position and continue



SECTION 18: do_generic_mapping_read() STEP-BY-STEP
================================================================================

STEP 1: Determine current page

    index  = *ppos >> PAGE_CACHE_SHIFT
    offset = *ppos & ~PAGE_CACHE_MASK

This identifies:
    which page in file
    where within page to start


STEP 2: Determine file size limits

    isize = i_size_read(inode)
    end_index = (isize - 1) >> PAGE_CACHE_SHIFT

If file is empty or index beyond EOF:
    stop


STEP 3: Readahead hinting

If current page equals next expected readahead trigger:
    page_cache_readahead(mapping, &ra, filp, index, ...)

This may submit asynchronous reads for future pages.


STEP 4: Find page in page cache

    page = find_get_page(mapping, index)

If missing:
    handle_ra_miss(...)
    go to no_cached_page


STEP 5: Ensure page is uptodate

If PageUptodate(page):
    good, proceed

Else:
    lock page
    if truncated while waiting, retry
    if someone else already made it uptodate, use it
    otherwise call:
        mapping->a_ops->readpage(filp, page)

Then wait/check until page becomes uptodate or error.


STEP 6: Copy bytes from page to consumer

Once page is valid:
    actor(desc, page, offset, nr)

For read():
    actor is file_read_actor()
    which copies bytes to user memory


STEP 7: Advance and continue

Update:
    offset
    index
    desc->count
    desc->written
    *ppos


This loop continues until:
    request satisfied
    EOF reached
    error occurs



SECTION 19: WHY PageUptodate IS CENTRAL
================================================================================

A page can exist in page cache but still not contain valid data yet.

Examples:

    page inserted but read not done yet
    read I/O in progress
    previous I/O failed
    truncate/race happened

So lookup alone is not enough.

The read path requires:

    page present
and
    PageUptodate(page) == 1

Only then may contents be copied to user space safely.

This is why so much of the read path revolves around:

    find page
    if not uptodate -> lock/fill/retry



SECTION 20: file_read_actor()
================================================================================

Purpose:

    copy bytes from a page-cache page to user buffer


Fast path:

    fault_in_pages_writeable(dest, size)
    kmap_atomic(page)
    __copy_to_user_inatomic(...)

Slow path:

    kmap(page)
    __copy_to_user(...)

It updates:

    desc->count
    desc->written
    desc->arg.buf
    desc->error if needed


This actor is plugged into do_generic_mapping_read() to implement ordinary
read() semantics.



SECTION 21: generic_file_aio_read()
================================================================================

Purpose:

    generic filesystem read routine for cached files


Flow:

    1) validate iovecs
    2) if O_DIRECT:
           try generic_file_direct_IO(READ,...)
           if it succeeds or returns nonzero, done
           if it returns zero, fall back to buffered
    3) for each iovec segment:
           initialize read_descriptor_t
           call do_generic_file_read(..., file_read_actor)
    4) return total bytes read or error


So the function is really:

    front-end validation + O_DIRECT decision + repeated buffered-read core


Important detail:

    direct I/O may partially or wholly satisfy the read,
    otherwise buffered read path is used.



SECTION 22: sendfile SUPPORT
================================================================================

Functions:

    file_send_actor()
    generic_file_sendfile()

Instead of copying to user memory, sendfile can push page contents to another
file/socket via that target file's sendpage() operation.

So generic_file_sendfile() reuses the same page-cache read core, but with a
different actor.

This is a beautiful design detail:

    same cache lookup/readpage logic
    different consumer of page bytes



SECTION 23: sys_readahead()
================================================================================

User space syscall:

    readahead(fd, offset, count)

Kernel flow:

    fget(fd)
    verify FMODE_READ
    convert byte range -> page range
    do_readahead(mapping, file, start, len)

which calls:

    force_page_cache_readahead(mapping, filp, index, nr)

This lets user space explicitly ask the kernel to populate the page cache
ahead of actual reads.



SECTION 24: page_cache_read()
================================================================================

Purpose:

    ensure page exists in page cache and start filesystem readpage()


Flow:

    allocate cold page
    add_to_page_cache_lru(page, mapping, offset, GFP_KERNEL)
    if success:
        mapping->a_ops->readpage(file, page)
    if -EEXIST:
        someone else inserted it, that is okay
    if AOP_TRUNCATED_PAGE:
        retry


This is a lower-level helper used by mmap fault and other paths when
a specific page must be pulled into cache.



SECTION 25: FILE-BACKED mmap PAGE FAULTS
================================================================================

Main function:

    filemap_nopage()

Purpose:

    service a page fault on a file-backed mapping


Scenario:

    process mmaps file
    process touches virtual address
    page fault occurs
    VMA uses generic_file_vm_ops.nopage = filemap_nopage
    filemap_nopage returns struct page to install


Overall flow:

    1) compute file page offset from fault address
    2) ensure fault is within file size
    3) maybe do mmap readahead / readaround
    4) find page in page cache
    5) if missing, page_cache_read()
    6) if not uptodate, lock and readpage()
    7) return page if success
    8) return SIGBUS/OOM on failure



SECTION 26: filemap_nopage() STEP-BY-STEP
================================================================================

STEP 1: Compute pgoff

    pgoff = ((address - area->vm_start) >> PAGE_CACHE_SHIFT) + area->vm_pgoff

This maps the faulting virtual address to a file page index.


STEP 2: Check against file size

If beyond file content:
    normal process gets NOPAGE_SIGBUS
    external ptracer special-case may differ


STEP 3: Readahead policy for mmap

If VM_RandomReadHint(area):
    skip normal readahead

If VM_SequentialReadHint(area):
    page_cache_readahead(mapping, ra, file, pgoff, 1)

If many misses but not pure sequential:
    do readaround around pgoff


STEP 4: Page cache lookup

    page = find_get_page(mapping, pgoff)

If missing:
    maybe issue readaround or page_cache_read()
    then retry


STEP 5: If page not uptodate

    lock_page(page)
    validate page still mapped
    call readpage()
    wait for unlock
    retry/check PageUptodate


STEP 6: Success

    mark_page_accessed(page)
    set *type = VM_FAULT_MINOR or MAJOR
    return page


This function is the generic file-backed fault loader for many filesystems.



SECTION 27: MINOR vs MAJOR FAULTS IN filemap_nopage()
================================================================================

If the page is already in cache and usable:
    usually VM_FAULT_MINOR

If disk I/O/readaround had to be triggered:
    VM_FAULT_MAJOR
    count_vm_event(PGMAJFAULT)

So page faults on mapped files distinguish:

    cache hit fault
vs
    I/O-requiring fault

This is why mmap performance depends heavily on warm page cache.



SECTION 28: filemap_getpage() and filemap_populate()
================================================================================

filemap_getpage()

    helper very similar to filemap_nopage read logic
    used to fetch one page, optionally nonblocking


filemap_populate()

    used for VMA population / prefaulting
    may pre-read pages
    fetches pages and installs them into the process page tables


Flow for filemap_populate():

    maybe force readahead
    check requested range against file size
    get page via filemap_getpage()
    install page into PTEs with install_page()
    for nonlinear nonblock case, install file PTE placeholder


So this is part of "populate this mapping now instead of faulting later."



SECTION 29: generic_file_mmap()
================================================================================

Purpose:

    generic mmap setup for normal filesystems


Checks:

    if mapping has no readpage:
        return -ENOEXEC

Otherwise:

    file_accessed(file)
    vma->vm_ops = &generic_file_vm_ops

where:

    generic_file_vm_ops = {
        .nopage   = filemap_nopage,
        .populate = filemap_populate,
    }


Meaning:

    future faults on this VMA will use the generic page-cache-backed fault path



SECTION 30: read_cache_page()
================================================================================

Purpose:

    read one specific page into page cache, filling it if necessary


Flow:

    __read_cache_page(...)
        find existing page or allocate/add one
        if newly created, call filler(data, page)

Then:

    if returned page not uptodate:
        lock page
        retry filler if needed


This is another reusable helper for filesystems/subsystems that need a page
by index and a supplied "fill" callback.



SECTION 31: SUID/SGID REMOVAL ON WRITE
================================================================================

Functions:

    should_remove_suid()
    __remove_suid()
    remove_suid()

Background:

When a regular file with setuid/setgid bits is modified, those privilege bits
often must be cleared so that stale privileged executables are not silently
changed and left privileged.

Logic:

    if S_ISUID -> kill suid
    if S_ISGID and executable group bit set -> kill sgid
    unless caller has CAP_FSETID


Write path later calls:

    remove_suid(file->f_path.dentry)

before performing the write.

This is a security-related side effect of writes.



SECTION 32: generic_write_checks()
================================================================================

Purpose:

    validate and adjust write position/count before actual write


Checks include:

    negative position -> -EINVAL
    O_APPEND -> move pos to inode size
    RLIMIT_FSIZE handling -> may signal SIGXFSZ / return -EFBIG
    MAX_NON_LFS checks for non-largefile
    filesystem maximum size s_maxbytes
    block device read-only / size limits


This function may shrink *count for a short write instead of outright failing.


So before any data copying happens, the kernel ensures:

    requested write range is legal



SECTION 33: GENERIC BUFFERED WRITE CORE
================================================================================

Main function:

    generic_file_buffered_write()

This is the heart of cached writes.

Overall flow:

    1) determine target page index and offset
    2) fault in user source pages first
    3) grab/create destination page-cache page
    4) call filesystem prepare_write()
    5) copy user bytes into page
    6) flush dcache if needed
    7) call filesystem commit_write()
    8) advance position/count
    9) throttle dirtying with balance_dirty_pages_ratelimited()
   10) optionally sync for O_SYNC / O_DIRECT fallback semantics


This path writes INTO PAGE CACHE, not directly to disk.



SECTION 34: WHY fault_in_pages_readable() HAPPENS BEFORE GRABBING DEST PAGE
================================================================================

In buffered write:

    fault_in_pages_readable(buf, bytes);

is done before locking/preparing the destination page.

Why?

To avoid nasty deadlocks when user buffer aliases the same page or when taking
faults while holding page locks/fs locks would recurse badly.

So the write path first ensures the source user memory is present/readable,
then proceeds with page-cache destination manipulation.



SECTION 35: __grab_cache_page()
================================================================================

Purpose:

    internal helper for buffered write
    find or create a locked page-cache page

Flow:

    try find_lock_page(mapping, index)
    if absent:
        allocate page if necessary
        add_to_page_cache()
        if success:
            extra ref
            queue it on caller's LRU pagevec buffering
            return page


This is optimized for repeated page acquisitions during writes.



SECTION 36: prepare_write() and commit_write()
================================================================================

In buffered write, the filesystem is given two hooks:

    a_ops->prepare_write(file, page, from, to)
    a_ops->commit_write(file, page, from, to)

prepare_write()

    ensure blocks are ready / filesystem state prepared
    may instantiate blocks
    may perform checks
    returns error if cannot proceed

commit_write()

    finalize the write into page cache
    update file size if needed
    mark buffers/page dirty as appropriate


Why split?

Because filesystem may need a setup phase before the actual bytes are copied.

This separation was common in older kernels.



SECTION 37: USER DATA COPY INTO PAGE CACHE
================================================================================

Functions used:

    filemap_copy_from_user(...)
    filemap_copy_from_user_iovec(...)
    __filemap_copy_from_user_iovec_inatomic(...)

The generic buffered write copies bytes from user iovecs into the page cache page.

After copy:

    flush_dcache_page(page)

Then commit_write() is called.


At this point data is in RAM page cache.
Disk writeback may happen later.



SECTION 38: DIRTY PAGE THROTTLING
================================================================================

Inside buffered write loop:

    balance_dirty_pages_ratelimited(mapping);

Why?

If writers dirty pages too fast, the kernel must slow them down so writeback
can catch up.

This connects directly to your earlier backing_dev_info / congestion questions.

Buffered writes are not "free":
they create dirty cache pressure and eventually storage traffic.

So filemap.c participates in global dirty-page balancing.



SECTION 39: O_SYNC IN BUFFERED WRITE
================================================================================

After buffered write succeeds:

    if O_SYNC or IS_SYNC(inode):
        generic_osync_inode(... OSYNC_METADATA|OSYNC_DATA)

And in the higher wrapper:

    sync_page_range() or sync_page_range_nolock()

So for synchronous files, buffered write is followed by actual writeback/wait,
not just dirtying the page cache.

This preserves application-visible sync semantics.



SECTION 40: DIRECT I/O WRITE FRONT-END
================================================================================

Function:

    generic_file_direct_write()

This handles O_DIRECT write semantics using:

    generic_file_direct_IO(WRITE, ...)

If write extends i_size:
    update inode size
    mark inode dirty

If O_SYNC / IS_SYNC:
    sync metadata with generic_osync_inode()


This is the direct-I/O side wrapper.
But direct I/O still has to coordinate with page cache, which leads to the
next function.



SECTION 41: generic_file_direct_IO()
================================================================================

Purpose:

    coordinate direct I/O with page cache to preserve correctness


WRITE flow:

    1) if mapping is memory-mapped:
           unmap_mapping_range(mapping, offset, write_len, 0)
       so dirty PTE state is propagated and mappings are not stale

    2) filemap_write_and_wait(mapping)
       flush and wait on existing dirty page cache data

    3) call mapping->a_ops->direct_IO(...)

    4) if WRITE and mapping still has pages in written range:
           invalidate_inode_pages2_range(...)
       to remove stale cached pages and preserve O_DIRECT semantics


This is critical.

Direct I/O bypasses page cache for data transfer,
but page cache may still hold stale overlapping pages.

So filemap.c makes sure:

    cache and direct I/O do not diverge incorrectly



SECTION 42: __generic_file_aio_write_nolock()
================================================================================

This is the main write front-end before wrapper locking behavior.

Flow:

    1) validate iovecs
    2) vfs_check_frozen()
    3) set current->backing_dev_info = mapping->backing_dev_info
    4) generic_write_checks()
    5) remove_suid()
    6) file_update_time()
    7) if O_DIRECT:
           generic_file_direct_write()
           maybe fall through to buffered for holes / partial case
           sync+invalidate buffered remainder if needed
       else:
           generic_file_buffered_write()
    8) clear current->backing_dev_info
    9) return bytes or error


Important subtlety:

    O_DIRECT write may fall through to buffered write
    for parts like hole instantiation

Then the code syncs and invalidates to try to preserve expected O_DIRECT behavior.



SECTION 43: generic_file_aio_write() vs _nolock()
================================================================================

generic_file_aio_write()

    takes inode->i_mutex around __generic_file_aio_write_nolock()
    then for O_SYNC calls sync_page_range()


generic_file_aio_write_nolock()

    does not take i_mutex
    then for O_SYNC calls sync_page_range_nolock()


So the core data movement is shared,
but locking context differs based on caller needs.



SECTION 44: filemap_write_and_wait()
================================================================================

Purpose:

    common helper to flush dirty data and wait for completion


Flow:

    if mapping has pages:
        err = filemap_fdatawrite(mapping)
        if err != -EIO:
            err2 = filemap_fdatawait(mapping)
            if !err:
                err = err2

Meaning:

    start writeback
    wait for completion/errors

Special case:

    if first phase returns -EIO, don't wait further

because that may indicate especially bad conditions.


This helper is used heavily before direct I/O and in sync-style paths.



SECTION 45: mmap + write + direct I/O COHERENCE
================================================================================

A major reason filemap.c is complicated is coherence between:

    page cache
    mmap PTEs
    direct I/O
    truncation
    buffered writes

Example problem:

    process mmaps file page
    another thread does O_DIRECT write to same range
    page cache still has old data
    mapped page still dirty or stale

So generic_file_direct_IO() does:

    unmap_mapping_range()
    filemap_write_and_wait()
    direct_IO()
    invalidate overlapping cached pages


This is the generic coherence choreography.



SECTION 46: RELEASEPAGE / PAGE RECLAIM INTERACTION
================================================================================

Function:

    try_to_release_page(page, gfp_mask)

Used when reclaim wants to free a page and first must release filesystem
private metadata attached to the page.

Flow:

    page must be locked
    if PageWriteback(page): cannot release
    if mapping has a_ops->releasepage:
        call that
    else:
        try_to_free_buffers(page)


So reclaim asks the filesystem:

    "can you drop buffers/private state so this page can be freed?"


This is another example of filemap.c sitting between generic VM and fs-specific
page metadata.



SECTION 47: COMPLETE BUFFERED READ FLOW DIAGRAM
================================================================================

User read()
    |
    v
generic_file_aio_read()
    |
    +--> validate iovecs
    |
    +--> if O_DIRECT try generic_file_direct_IO(READ)
    |        |
    |        +--> if satisfied, done
    |
    v
for each iovec:
    |
    v
do_generic_mapping_read()
    |
    +--> compute page index/offset
    +--> maybe trigger readahead
    +--> find_get_page(mapping, index)
            |
            +--> page present + uptodate?
            |        |
            |        +--> yes: actor copies to user
            |
            +--> missing?
            |        |
            |        +--> allocate/add page to cache
            |        +--> readpage()
            |
            +--> present but not uptodate?
                     |
                     +--> lock page
                     +--> readpage() / wait
                     +--> actor copies to user
    |
    v
advance ppos / desc
    |
    v
return bytes



SECTION 48: COMPLETE BUFFERED WRITE FLOW DIAGRAM
================================================================================

User write()
    |
    v
generic_file_aio_write()
    |
    +--> mutex_lock(i_mutex)
    |
    v
__generic_file_aio_write_nolock()
    |
    +--> validate iovecs
    +--> generic_write_checks()
    +--> remove_suid()
    +--> file_update_time()
    +--> choose O_DIRECT or buffered
             |
             +--> buffered:
                     |
                     v
                generic_file_buffered_write()
                     |
                     +--> fault_in source user pages
                     +--> __grab_cache_page()
                     +--> prepare_write()
                     +--> copy user bytes -> page cache
                     +--> flush_dcache_page()
                     +--> commit_write()
                     +--> mark/access/update positions
                     +--> balance_dirty_pages_ratelimited()
    |
    +--> mutex_unlock(i_mutex)
    |
    +--> if O_SYNC / IS_SYNC:
            sync_page_range()
    |
    v
return bytes



SECTION 49: COMPLETE mmap PAGE FAULT FLOW DIAGRAM
================================================================================

Process accesses mmap'd file address
    |
    v
page fault
    |
    v
filemap_nopage()
    |
    +--> compute file pgoff from address
    +--> verify within file size
    +--> maybe do mmap readahead / readaround
    +--> find_get_page(mapping, pgoff)
            |
            +--> miss:
            |      page_cache_read()
            |      retry lookup
            |
            +--> hit but not uptodate:
            |      lock page
            |      readpage()
            |      wait / retry
            |
            +--> hit and uptodate:
                   mark_page_accessed()
                   return page
    |
    v
fault handler installs PTE



SECTION 50: WHAT THIS SOURCE FILE ACHIEVES
================================================================================

This file achieves generic cached file I/O by combining:

    radix-tree page cache management
    page locking/wakeup
    readahead integration
    filesystem callbacks
    writeback helpers
    mmap fault servicing
    direct-I/O/page-cache coherence
    buffered read/write loops

It hides huge amounts of complexity from individual filesystems.

A filesystem that plugs in suitable address_space operations can reuse all this
generic machinery instead of re-implementing:

    page-cache lookup logic
    buffered read path
    buffered write path
    file-backed mmap fault path
    writeback waiting helpers



SECTION 51: SIMPLE MENTAL MODEL
================================================================================

Think of filemap.c as the operating system's generic librarian for file pages.

For reads:

    "Do we already have this book page on the desk?"
    if no:
        "fetch it from storage"
    then:
        "hand requested bytes to the reader"


For writes:

    "Get the right page on the desk"
    "prepare it for edits"
    "copy user's edits into it"
    "mark it to be written back later"


For mmap faults:

    "Someone touched a mapped page"
    "find or fetch that page"
    "give it to the MM subsystem to map"


For direct I/O:

    "clear conflicting cached state first"
    "do direct transfer"
    "invalidate stale cache afterward"



SECTION 52: SUMMARY
================================================================================

mm/filemap.c is one of the core generic file-cache engines in Linux.

Main responsibilities:

    manage file pages in page cache
    support generic buffered reads
    support generic buffered writes
    service file-backed mmap faults
    start and wait for file writeback
    provide page cache lookup/add helpers
    coordinate direct I/O with cached pages
    help reclaim drop filesystem page-private state

Key ideas to remember:

    address_space is the file's cache container
    pages are indexed in mapping->page_tree by file page index
    PageUptodate determines whether cached contents are valid
    page lock serializes I/O/fill/truncate against each page
    readpage/prepare_write/commit_write/direct_IO are filesystem callbacks
    readahead and writeback integrate through this generic layer

In short:

    filemap.c is the generic page-cache implementation that makes normal
    Linux file I/O and file-backed mmap work efficiently and coherently.


================================================================================
END OF FILE
================================================================================
