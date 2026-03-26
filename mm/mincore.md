================================================================================
FILE: mincore.c — MINCORE(2) FLOW AND PAGE CACHE RESIDENCY
SOURCE: user file :contentReference[oaicite:0]{index=0}
================================================================================


SECTION 0: WHAT THIS FILE DOES
================================================================================

This file implements:

    sys_mincore()

Purpose:

    "Tell userspace which pages in a virtual address range are currently
     resident / already available in memory"

But in this old implementation, "resident" is approximated as:

    page exists in page cache
    AND
    page is Uptodate

So this is NOT a full generic "all virtual pages are resident?" checker.

It is mainly checking file-backed mappings.


================================================================================
SECTION 1: BIG PICTURE
================================================================================

User calls:

    mincore(start, len, vec)

Kernel does:

    1. validate arguments
    2. walk the mapped range chunk by chunk
    3. for each page:
           look in page cache
           check PageUptodate()
    4. write 1-byte status back into vec[]


--------------------------------------------------------------------------------
VERY IMPORTANT LIMITATION
--------------------------------------------------------------------------------

This implementation rejects anonymous mappings:

    if (!vma->vm_file)
        return -ENOMEM;

So here mincore() is basically:

    "Is this file-backed page already in page cache and uptodate?"


================================================================================
SECTION 2: TOP-LEVEL FLOW
================================================================================

sys_mincore()
    |
    |-- validate start alignment
    |-- validate user addresses
    |-- compute number of pages
    |-- allocate one temporary kernel page buffer
    |
    |-- while pages remain:
    |       down_read(mmap_sem)
    |       do_mincore(...)
    |       up_read(mmap_sem)
    |
    |       copy temporary results to userspace vec
    |
    |-- free temp buffer
    |-- return


================================================================================
SECTION 3: WHAT "IN CORE" MEANS HERE
================================================================================

mincore_page(vma, pgoff):

    page = find_get_page(mapping, pgoff)
    if (page) {
        present = PageUptodate(page)
        page_cache_release(page)
    }

So the test is:

    present = 1   if page exists in page cache AND is uptodate
    present = 0   otherwise


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

file mapping
    |
    +--> address_space (page cache)
            |
            +--> page at given pgoff?
                    |
                    +--> no  -> vec[i] = 0
                    |
                    +--> yes
                          |
                          +--> PageUptodate ?
                                 |
                                 +--> yes -> vec[i] = 1
                                 +--> no  -> vec[i] = 0


================================================================================
SECTION 4: mincore_page()
================================================================================

--------------------------------------------------------------------------------
CODE INTENT
--------------------------------------------------------------------------------

static unsigned char mincore_page(struct vm_area_struct *vma,
                                  unsigned long pgoff)

Inputs:

    vma   = virtual memory area
    pgoff = file page offset within mapped file

Flow:

    as = vma->vm_file->f_mapping
    page = find_get_page(as, pgoff)

If page exists:
    present = PageUptodate(page)

Then drop page ref.

Return:

    0 or 1


--------------------------------------------------------------------------------
IMPORTANT NOTE
--------------------------------------------------------------------------------

This does NOT fault pages in.
This does NOT start I/O.
This is only a lookup.


================================================================================
SECTION 5: do_mincore()
================================================================================

This is the inner worker that handles one chunk.

Signature:

    do_mincore(addr, vec, pages)

Inputs:

    addr  = starting user virtual address
    vec   = temporary kernel buffer where output bytes go
    pages = how many pages to inspect in this chunk


--------------------------------------------------------------------------------
FLOW
--------------------------------------------------------------------------------

1. Find VMA covering addr

       vma = find_vma(current->mm, addr)

2. Reject if:
       no VMA
       or addr lies in a hole before vma->vm_start

3. Reject anonymous mappings
       if (!vma->vm_file) return -ENOMEM

4. Compute how many pages can be checked inside this VMA

5. Translate virtual address to file page offset:

       pgoff = (addr - vma->vm_start) >> PAGE_SHIFT
       pgoff += vma->vm_pgoff

6. For each page:
       vec[i] = mincore_page(vma, pgoff+i)

7. Return nr pages processed


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

virtual addr
    |
    v
find_vma(mm, addr)
    |
    +--> hole / unmapped -> -ENOMEM
    |
    +--> vma found
            |
            +--> anonymous? -> -ENOMEM
            |
            +--> file-backed
                    |
                    +--> compute pgoff
                    +--> fill vec[]


================================================================================
SECTION 6: ADDRESS TO FILE OFFSET TRANSLATION
================================================================================

This is important.

For a file mapping:

    vma->vm_start ....... virtual start
    vma->vm_pgoff ....... file offset in pages where mapping starts

For any virtual address addr inside that VMA:

    page offset inside VMA:
        (addr - vma->vm_start) >> PAGE_SHIFT

Then add mapping's file start offset:

    pgoff = above + vma->vm_pgoff


--------------------------------------------------------------------------------
ASCII EXAMPLE
--------------------------------------------------------------------------------

Suppose:

    vma->vm_start = 0x400000
    vma->vm_pgoff = 100 pages
    addr          = 0x402000

Then:

    offset inside vma = (0x402000 - 0x400000) / 4096 = 2
    file pgoff        = 100 + 2 = 102

So mincore checks page cache page 102 of the file.


================================================================================
SECTION 7: sys_mincore()
================================================================================

This is the syscall wrapper.

Signature:

    sys_mincore(start, len, vec)


--------------------------------------------------------------------------------
STEP-BY-STEP
--------------------------------------------------------------------------------

1. Check start is page aligned

       if (start & ~PAGE_CACHE_MASK)
           return -EINVAL

2. Check [start, start+len) is valid userspace range

       access_ok(VERIFY_READ, start, len)

3. Compute number of pages:

       pages = len >> PAGE_SHIFT
       pages += (len & ~PAGE_MASK) != 0

Meaning:
    round up partial last page

4. Check vec buffer writable

       access_ok(VERIFY_WRITE, vec, pages)

5. Allocate one temporary kernel page:

       tmp = __get_free_page(GFP_USER)

This tmp holds up to PAGE_SIZE output bytes at a time.

6. Loop while pages remain:
       do_mincore(start, tmp, min(pages, PAGE_SIZE))
       copy_to_user(vec, tmp, retval)

7. Advance:
       pages -= retval
       vec   += retval
       start += retval << PAGE_SHIFT

8. Free temp page and return


================================================================================
SECTION 8: WHY CHUNKING EXISTS
================================================================================

The kernel does not directly fill user vec one byte at a time under mmap_sem.

Instead it uses:

    one temporary kernel page buffer

Because:

    tmp can hold PAGE_SIZE bytes
    each byte describes one page

So one iteration handles at most:

    PAGE_SIZE pages

For 4KB page size:

    tmp size = 4096 bytes
    so one loop can report 4096 pages
    which corresponds to 4096 * 4KB = 16MB of virtual range


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

tmp kernel page:
    [byte0][byte1][byte2]...[byte4095]

Each byte:
    page residency for one virtual page


================================================================================
SECTION 9: LOCKING
================================================================================

During each chunk:

    down_read(&current->mm->mmap_sem)
        do_mincore(...)
    up_read(&current->mm->mmap_sem)

Why?

Because VMA layout must remain stable while:

    find_vma()
    vm_start / vm_end usage
    vm_file lookup
    vm_pgoff computation


--------------------------------------------------------------------------------
NOTE
--------------------------------------------------------------------------------

This lock protects virtual mapping metadata,
not page cache residency from changing afterwards.

That is why the file comments say result may be stale.


================================================================================
SECTION 10: RETURN VALUE SEMANTICS
================================================================================

Possible returns:

    0        success after full processing
    -EINVAL  start not page aligned
    -ENOMEM  unmapped range / unsupported mapping / bad user range
    -EFAULT  vec buffer bad
    -EAGAIN  temporary kernel memory allocation failed


--------------------------------------------------------------------------------
IMPORTANT SUBTLETY
--------------------------------------------------------------------------------

Inside the while loop:

    retval = do_mincore(...)

If do_mincore returns positive N:
    that means "processed N pages"

If it returns <= 0:
    that is an error or end condition


================================================================================
SECTION 11: WHY ANONYMOUS MAPPINGS ARE REJECTED
================================================================================

This code says:

    if (!vma->vm_file)
        return -ENOMEM;

Meaning:

    only file-backed mappings supported

The comment itself admits this is weak / historical:

    "We should just look at the page tables"

So in this implementation, mincore is not a general VM residency syscall.
It is more like a page-cache-backed-file residency check.


================================================================================
SECTION 12: WHAT THIS FILE DOES *NOT* DO
================================================================================

It does NOT:

    ✘ walk page tables for anonymous pages
    ✘ trigger page faults
    ✘ trigger read-ahead
    ✘ bring pages into memory
    ✘ guarantee future residency
    ✘ distinguish "mapped but not uptodate" from "not in cache" in output

It only checks:

    "find_get_page + PageUptodate"


================================================================================
SECTION 13: RELATION TO PAGE CACHE
================================================================================

This file depends on filemap/page cache behavior:

    address_space
        -> page cache radix tree
        -> file page lookup

So conceptually:

    mincore() here is asking page cache:
        "Do you already have this file page ready?"


--------------------------------------------------------------------------------
Connection to filemap.c
--------------------------------------------------------------------------------

find_get_page(as, pgoff)

comes from page-cache machinery.

So this file is a thin syscall wrapper around page-cache lookup.


================================================================================
SECTION 14: END-TO-END EXAMPLE
================================================================================

Userspace:

    mmap(file)
    mincore(mapped_addr, len, vec)

Kernel:

    sys_mincore()
        |
        +--> validate
        |
        +--> do_mincore()
                |
                +--> find_vma(addr)
                +--> verify file-backed
                +--> convert addr -> file pgoff
                +--> for each page:
                        find_get_page(mapping, pgoff)
                        check PageUptodate
                        vec[i] = 0/1
        |
        +--> copy result to user vec


--------------------------------------------------------------------------------
ASCII EXAMPLE
--------------------------------------------------------------------------------

File pages:
    [100] [101] [102] [103]

Page cache:
    [100 present uptodate]
    [101 absent]
    [102 present but not uptodate]
    [103 present uptodate]

Returned vec:
    [1] [0] [0] [1]


================================================================================
SECTION 15: MOST IMPORTANT FUNCTIONS
================================================================================

1. mincore_page()
       actual page cache lookup for one file page

2. do_mincore()
       VMA validation + addr→pgoff translation + loop

3. sys_mincore()
       syscall entry, chunking, copy_to_user


================================================================================
SECTION 16: INTERVIEW / UNDERSTANDING POINTS
================================================================================

1) mincore does not mean "guaranteed in RAM forever"
   It is just a snapshot.

2) In this implementation, residency means:
       page exists in page cache and is uptodate

3) Anonymous memory is not handled here.

4) The syscall reports one byte per page.

5) VMA metadata is protected by mmap_sem during lookup.


================================================================================
SECTION 17: ONE-LINE SUMMARY
================================================================================

    mincore.c implements a snapshot-style residency query for file-backed
    mappings by checking whether each file page is currently present and
    uptodate in the page cache.


================================================================================
END
================================================================================
