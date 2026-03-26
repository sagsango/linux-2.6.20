================================================================================
FILE: linux_fadvise_background_and_flow.txt
TOPIC: File Access Advice (mm/fadvise.c) and How It Works
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHY fadvise EXISTS
================================================================================

Applications often know their future file access pattern better than the kernel.

Examples:

    1) "I will read this file sequentially"
    2) "I will access this file randomly"
    3) "I will need this range soon"
    4) "I no longer need these cached pages"

The kernel page cache and readahead logic try to guess access patterns,
but guesses are not always accurate.

So Linux provides:

    posix_fadvise()

which lets user space give HINTS to the kernel.

Important:

    fadvise does NOT usually force correctness changes.
    It mainly influences caching / readahead / page-cache behavior.


This file implements the system call handler:

    sys_fadvise64_64()



SECTION 2: BIG PICTURE
================================================================================

User space calls:

    posix_fadvise(fd, offset, len, advice)

glibc eventually reaches kernel syscall:

    sys_fadvise64_64(fd, offset, len, advice)

The kernel then:

    1) validates file and arguments
    2) finds file's mapping and backing device
    3) interprets the advice type
    4) adjusts readahead state or page cache behavior


This is a PERFORMANCE hint interface, not a data-access syscall.



SECTION 3: WHAT IS BEING ADVISED
================================================================================

The advice applies to a file and often to a byte range.

Inputs:

    fd       = file descriptor
    offset   = starting byte offset
    len      = length in bytes
    advice   = one of POSIX_FADV_*


Main advice types in this code:

    POSIX_FADV_NORMAL
    POSIX_FADV_RANDOM
    POSIX_FADV_SEQUENTIAL
    POSIX_FADV_WILLNEED
    POSIX_FADV_NOREUSE
    POSIX_FADV_DONTNEED



SECTION 4: IMPORTANT OBJECTS IN THIS FUNCTION
================================================================================

struct file *file

    kernel object for the open file descriptor


struct address_space *mapping

    page-cache mapping for the file
    this represents cached pages for that file


struct backing_dev_info *bdi

    backing device info for the file's storage backend
    contains device readahead defaults and congestion info


file->f_ra.ra_pages

    per-file readahead setting


mapping->a_ops

    address_space operations
    e.g. readpage, get_xip_page


So this syscall mostly manipulates:

    per-file readahead settings
    page cache state for a file mapping



SECTION 5: OVERALL FLOW OF sys_fadvise64_64()
================================================================================

High-level flow:

    user calls posix_fadvise()
            |
            v
    kernel finds struct file from fd
            |
            v
    validate file and range
            |
            v
    get mapping and backing_dev_info
            |
            v
    switch(advice)
            |
            +--> NORMAL      -> restore default readahead
            |
            +--> RANDOM      -> disable readahead
            |
            +--> SEQUENTIAL  -> enlarge readahead
            |
            +--> WILLNEED    -> trigger readahead now
            |
            +--> NOREUSE     -> currently mostly no-op here
            |
            +--> DONTNEED    -> flush/writeback if needed, invalidate cache pages
            |
            v
    return result



SECTION 6: STEP 1 - GET THE FILE
================================================================================

Code:

    struct file *file = fget(fd);

Purpose:

    convert user file descriptor into kernel struct file


If fd is invalid:

    return -EBADF


Meaning:

    the syscall cannot proceed without a valid open file



SECTION 7: STEP 2 - REJECT PIPES/FIFOS
================================================================================

Code:

    if (S_ISFIFO(file->f_path.dentry->d_inode->i_mode)) {
        ret = -ESPIPE;
        goto out;
    }

Why?

fadvise is meaningful for seekable file-like objects with page cache /
file offsets.

A FIFO/pipe does not behave like a normal seekable file.

So:

    advice on FIFO -> -ESPIPE



SECTION 8: STEP 3 - GET THE FILE MAPPING
================================================================================

Code:

    mapping = file->f_mapping;

This is the page-cache mapping for the file.

If no mapping exists or len < 0:

    ret = -EINVAL

Why does mapping matter?

Because WILLNEED and DONTNEED operate on the page cache pages of the file.

Without a mapping, there is nothing meaningful to advise.



SECTION 9: STEP 4 - XIP SPECIAL CASE
================================================================================

Code:

    if (mapping->a_ops->get_xip_page)
        goto out;

XIP means:

    execute-in-place

Some special filesystems/devices can access file contents directly
without normal page-cache semantics.

In that case, usual page-cache-oriented advice does not apply well.

So the kernel simply ignores the advice and returns success-like behavior.

Meaning:

    no error, but no meaningful action



SECTION 10: STEP 5 - COMPUTE THE END BYTE SAFELY
================================================================================

Code:

    endbyte = offset + len;
    if (!len || endbyte < len)
        endbyte = -1;
    else
        endbyte--;

Purpose:

    compute inclusive end byte of advised range


Important rule here:

    len == 0 means "from offset to as much as possible"


Why the overflow check?

If offset + len overflows a signed 64-bit range, the code treats it as:

    all remaining bytes

So:

    len == 0 or overflow -> endbyte = -1

Otherwise:

    endbyte = offset + len - 1


This "inclusive endbyte" is later converted into page indexes.



SECTION 11: STEP 6 - GET THE BACKING DEVICE INFO
================================================================================

Code:

    bdi = mapping->backing_dev_info;

This gives access to:

    default readahead size
    congestion state

It is used in:

    NORMAL
    SEQUENTIAL
    DONTNEED


So fadvise is not only about the page cache;
it also adapts behavior based on the storage device defaults/state.



SECTION 12: POSIX_FADV_NORMAL
================================================================================

Code:

    file->f_ra.ra_pages = bdi->ra_pages;

Meaning:

    restore normal/default readahead behavior for this file


Background:

Linux keeps readahead state per file.

If earlier the app said RANDOM or SEQUENTIAL, that may have changed the
file's readahead behavior.

NORMAL says:

    "go back to the default policy"

The default comes from the backing device:

    bdi->ra_pages



SECTION 13: POSIX_FADV_RANDOM
================================================================================

Code:

    file->f_ra.ra_pages = 0;

Meaning:

    disable readahead for this file


Why?

If an application will access pages randomly, readahead is often wasteful.

Sequential logic would otherwise fetch nearby pages that may never be used.

So RANDOM tells the kernel:

    "do not speculate ahead"

Effect:

    future reads are less likely to trigger clustered readahead



SECTION 14: POSIX_FADV_SEQUENTIAL
================================================================================

Code:

    file->f_ra.ra_pages = bdi->ra_pages * 2;

Meaning:

    increase readahead aggressiveness


Why?

If the app says it will read sequentially, extra readahead is useful.

That reduces stalls because future pages are fetched before they are needed.

So SEQUENTIAL says:

    "this file is likely being consumed linearly"

The kernel responds by making readahead larger than default.



SECTION 15: POSIX_FADV_WILLNEED
================================================================================

This is the most active advice in the file.

Meaning:

    "I will need this range soon; please start pulling it into cache"


The code first checks:

    if (!mapping->a_ops->readpage)
        ret = -EINVAL;

Why?

If the mapping cannot read pages into the page cache, WILLNEED makes no sense.


Then it computes page indexes.


First page:

    start_index = offset >> PAGE_CACHE_SHIFT

Last page:

    end_index = endbyte >> PAGE_CACHE_SHIFT


These are PAGE CACHE page indices, not byte offsets.


Then:

    nrpages = end_index - start_index + 1;

If that overflows to zero:

    nrpages = ~0UL


Then the kernel calls:

    force_page_cache_readahead(mapping, file, start_index,
                               max_sane_readahead(nrpages));


Meaning:

    start readahead for this mapping from start_index
    for a bounded number of pages


If return value is positive:

    convert it to success (ret = 0)

Why?

A positive return here is not treated as an error for fadvise semantics.



SECTION 16: WILLNEED FLOW DIAGRAM
================================================================================

User:

    posix_fadvise(fd, offset, len, POSIX_FADV_WILLNEED)
            |
            v
Kernel:
    validate mapping and readpage support
            |
            v
    convert byte range -> page index range
            |
            v
    compute nrpages
            |
            v
    call force_page_cache_readahead()
            |
            v
    page-cache pages begin to get read in
            |
            v
    future actual reads may hit cache



SECTION 17: POSIX_FADV_NOREUSE
================================================================================

Code:

    case POSIX_FADV_NOREUSE:
        break;

In this kernel version, it is effectively a no-op.

Background idea of NOREUSE:

    "I may access these pages once; don't try too hard to keep them hot"

In theory the kernel could reduce page-cache value of such pages.

But in this implementation:

    no concrete action is taken


So it is accepted, but nothing substantial happens.



SECTION 18: POSIX_FADV_DONTNEED
================================================================================

This advice means:

    "I do not expect to need these cached pages anymore"

It does NOT mean:

    delete file data from disk

It means:

    release page-cache residency for the specified range if possible


The code first does:

    if (!bdi_write_congested(mapping->backing_dev_info))
        filemap_flush(mapping);

Why flush first?

If pages are dirty, dropping them from cache directly would lose changes.
So the kernel tries to push dirty file pages out first.

But only if the backing device is not currently write-congested.

If storage is already congested, aggressive flush is avoided.



SECTION 19: WHY bdi_write_congested() IS CHECKED
================================================================================

This is where your earlier backing_dev_info discussion connects.

If the underlying storage is congested:

    writeback queues are already overloaded

Then forcing extra flush work may worsen stalls.

So DONTNEED uses this logic:

    if device not congested:
        flush mapping
    then invalidate clean full pages in the range


This is a balance between:

    freeing page cache
and
    not overloading storage



SECTION 20: DONTNEED PAGE RANGE CALCULATION
================================================================================

Code:

    start_index = (offset + (PAGE_CACHE_SIZE - 1)) >> PAGE_CACHE_SHIFT;
    end_index   = (endbyte >> PAGE_CACHE_SHIFT);

Notice this differs from WILLNEED.

Why?

Because for DONTNEED the code targets:

    First and last FULL page


That means:

    partial first page is NOT invalidated
    partial last page handling is conservative


More precisely:

    start_index = round UP start to next full page
    end_index   = round DOWN end to containing page


This avoids dropping partially covered cache pages that may still contain
useful/unrelated file data outside the exact advised range.



SECTION 21: DONTNEED INVALIDATION
================================================================================

Code:

    if (end_index >= start_index)
        invalidate_mapping_pages(mapping, start_index, end_index);

Meaning:

    remove eligible pages in this page-index range from page cache


Important:

    only pages that can be invalidated are removed
    dirty/busy/pinned pages may not disappear immediately


So DONTNEED is best understood as:

    "please discard these cached pages if practical"



SECTION 22: DONTNEED FLOW DIAGRAM
================================================================================

User:

    posix_fadvise(fd, offset, len, POSIX_FADV_DONTNEED)
            |
            v
Kernel:
    get mapping and backing device
            |
            v
    if backing device not write-congested:
        filemap_flush(mapping)
            |
            v
    convert byte range -> full-page index range
            |
            v
    invalidate_mapping_pages(mapping, start_index, end_index)
            |
            v
    clean cache pages in that range may be dropped



SECTION 23: WHY FULL PAGES MATTER
================================================================================

Suppose the user says:

    offset = middle of page 10
    len    = up to middle of page 20

If the kernel invalidated page 10 or page 20 blindly, it could evict data
outside the exact logical range the user mentioned.

So DONTNEED only targets pages fully covered by the advised range.

That is why the comment says:

    First and last FULL page!


This is a correctness / conservatism detail.



SECTION 24: DEFAULT CASE
================================================================================

Code:

    default:
        ret = -EINVAL;

If advice is not one of the supported POSIX_FADV_* values:

    return -EINVAL



SECTION 25: CLEANUP
================================================================================

At the end:

    fput(file);
    return ret;

The kernel drops the file reference acquired by fget().

This happens for all paths through:

    goto out



SECTION 26: THE SMALL WRAPPER sys_fadvise64()
================================================================================

At the bottom:

    sys_fadvise64(int fd, loff_t offset, size_t len, int advice)

just forwards to:

    sys_fadvise64_64(fd, offset, len, advice)

This exists for architectures that want the older syscall entry shape.

So:

    sys_fadvise64() is just a compatibility wrapper



SECTION 27: COMPLETE END-TO-END FLOW
================================================================================

Application
    |
    | posix_fadvise(fd, offset, len, advice)
    v
glibc wrapper
    |
    v
sys_fadvise64_64()
    |
    +--> validate fd
    |
    +--> reject FIFO / bad mapping / negative len
    |
    +--> ignore XIP mappings
    |
    +--> compute endbyte safely
    |
    +--> get mapping + bdi
    |
    +--> switch(advice)
            |
            +--> NORMAL
            |      restore default readahead
            |
            +--> RANDOM
            |      disable readahead
            |
            +--> SEQUENTIAL
            |      enlarge readahead
            |
            +--> WILLNEED
            |      trigger readahead into page cache
            |
            +--> NOREUSE
            |      no-op here
            |
            +--> DONTNEED
            |      flush if not congested, invalidate full cached pages
            |
            +--> invalid advice -> -EINVAL
    |
    v
fput(file)
    |
    v
return to user



SECTION 28: WHAT THIS CODE ACHIEVES
================================================================================

This code lets user space shape kernel cache behavior for a file.

It achieves that by:

    changing per-file readahead size
    explicitly triggering readahead
    dropping page-cache pages that are no longer needed
    avoiding aggressive flush under write congestion

This helps workloads such as:

    database engines
    media streaming
    file scanners
    large sequential readers
    backup/indexing tools



SECTION 29: EXAMPLES OF REAL EFFECT
================================================================================

Example 1: Sequential file scan

    fadvise(SEQUENTIAL)

Effect:

    readahead grows
    fewer blocking reads during linear scan


Example 2: Random B-tree/database access

    fadvise(RANDOM)

Effect:

    stop useless speculative readahead


Example 3: Prefetch before use

    fadvise(WILLNEED)

Effect:

    pages begin entering cache before actual read()


Example 4: Large scan then discard

    read huge file
    fadvise(DONTNEED)

Effect:

    cache pollution reduced
    memory can be used for other workloads



SECTION 30: SIMPLE MENTAL MODEL
================================================================================

Think of the page cache as a library desk.

NORMAL:
    use the normal librarian behavior

RANDOM:
    stop bringing nearby books automatically

SEQUENTIAL:
    bring extra nearby books because I will probably need them

WILLNEED:
    bring these books to the desk now

DONTNEED:
    put these books back on the shelf

NOREUSE:
    I probably won't ask for them again soon
    (though in this kernel version, the librarian ignores that hint)



SECTION 31: SUMMARY
================================================================================

This source file implements the kernel side of posix_fadvise().

Main ideas:

    POSIX_FADV_* values are performance hints
    advice operates through struct file, address_space, and backing_dev_info
    NORMAL/RANDOM/SEQUENTIAL tune readahead
    WILLNEED proactively starts page-cache readahead
    DONTNEED flushes if sensible and invalidates full cached pages
    NOREUSE is effectively a no-op in this version

In short:

    fadvise lets applications tell the kernel how they plan to use a file,
    so the kernel can make better page-cache and readahead decisions.


================================================================================
END OF FILE
================================================================================
