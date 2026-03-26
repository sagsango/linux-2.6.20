/*
 * Linux 2.6.20 — mm/shmem.c
 *
 * IDE NOTES (single file, code-style)
 *
 * THIS IS THE CORE TMPFS IMPLEMENTATION
 *
 * Focus:
 *  - BIG PICTURE of tmpfs (VERY IMPORTANT)
 *  - How shmem differs from normal FS
 *  - Page lifecycle (RAM <-> SWAP)
 *  - Core data structures
 *  - shmem_getpage() (THE HEART)
 *  - Swap vector (direct + indirect)
 *  - Truncate, writepage, swapin/out flows
 *  - Read/write syscalls path
 *
 * Source: mm/shmem.c fileciteturn5file0
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
shmem/tmpfs = MEMORY-BACKED FILESYSTEM

But NOT just RAM:

    RAM  <->  SWAP


So tmpfs behaves like:

    filesystem interface
        +
    virtual memory system


Key idea:

    File data lives in:
        - page cache (RAM)
        - OR swap (if evicted)
*/


/*
Comparison:

ramfs:
    RAM only (no limits, no swap)

shmem/tmpfs:
    RAM + swap
    + accounting
    + limits
*/


/***************************************************************
 * 1. CORE DATA STRUCTURE
 ***************************************************************/

/*
struct shmem_inode_info

Holds:

    i_direct[]     → direct swap entries
    i_indirect     → indirect blocks (tree)

    swapped        → # pages in swap
    alloced        → # pages allocated

    next_index     → file size in pages

    flags          → SHMEM_PAGEIN / TRUNCATE / etc
*/


/***************************************************************
 * 2. STORAGE MODEL (VERY IMPORTANT)
 ***************************************************************/

/*
Each file page (index) can be:

    1. In page cache (RAM)
    2. In swap (swap entry stored in inode)
    3. Not allocated (hole)
*/


/*
Mapping:

    file offset → page index

    index → either:
        page cache entry
        OR swap entry
*/


/***************************************************************
 * 3. SWAP VECTOR DESIGN (VERY IMPORTANT)
 ***************************************************************/

/*
shmem stores swap entries in a hybrid structure:

1. DIRECT entries:

    info->i_direct[SHMEM_NR_DIRECT]

2. INDIRECT tree:

    info->i_indirect → pages of pointers


Structure:

    i_direct → small files

    i_indirect → large files (multi-level)
*/


/*
Hierarchy (simplified):

    i_indirect
        ├── direct blocks
        ├── double indirect
        └── triple indirect
*/


/***************************************************************
 * 4. shmem_getpage() (THE HEART)
 ***************************************************************/

/*
Function:

    shmem_getpage(inode, index, page*, sgp, type)


This function handles EVERYTHING:

    - lookup page in cache
    - swap-in
    - allocate new page
*/


/*
Flow:

1. Check page cache

2. If page in swap:
       → read from swap (swapin)
       → move to page cache

3. If no page exists:
       → allocate new page
       → zero-fill

4. Return locked, uptodate page
*/


/*
Key insight:

    This is BOTH:
        page fault handler
        read path
        write preparation
*/


/***************************************************************
 * 5. SWAP-IN FLOW
 ***************************************************************/

/*
If swap entry exists:

    lookup_swap_cache()

    if not present:
        read_swap_cache_async()

    then:
        move_from_swap_cache()
        free swap entry
*/


/*
IMPORTANT:

    page cannot exist in BOTH:
        swap cache AND page cache
*/


/***************************************************************
 * 6. ALLOCATION FLOW
 ***************************************************************/

/*
If no swap entry:

    allocate page:
        shmem_alloc_page()

    add to page cache:
        add_to_page_cache_lru()

    mark uptodate
*/


/***************************************************************
 * 7. WRITEBACK → SWAP (VERY IMPORTANT)
 ***************************************************************/

/*
Function:
    shmem_writepage()


Flow:

1. get_swap_page()

2. move_to_swap_cache(page)

3. store swap entry in inode

4. free page from RAM
*/


/*
Meaning:

    tmpfs writeback = swap-out

NOT disk writeback
*/


/***************************************************************
 * 8. TRUNCATE FLOW
 ***************************************************************/

/*
Function:
    shmem_truncate_range()


Steps:

1. remove pages from page cache

2. free swap entries

3. free indirect structures

4. update accounting
*/


/***************************************************************
 * 9. shmem_unuse() (SWAP-IN REVERSE)
 ***************************************************************/

/*
Used when system tries to reclaim swap entries:

    find inode using swap entry
    bring page back into memory
*/


/***************************************************************
 * 10. FILE READ PATH
 ***************************************************************/

/*
read() → shmem_file_read()
        → do_shmem_file_read()
            → shmem_getpage()
            → copy to user
*/


/***************************************************************
 * 11. FILE WRITE PATH
 ***************************************************************/

/*
write() → shmem_file_write()

loop:
    shmem_getpage()
    copy_from_user()
    set_page_dirty()
*/


/***************************************************************
 * 12. MMAP PATH
 ***************************************************************/

/*
mmap → shmem_mmap()
        → vma->vm_ops = shmem_vm_ops

page fault:
    shmem_nopage()
        → shmem_getpage()
*/


/***************************************************************
 * 13. MEMORY ACCOUNTING
 ***************************************************************/

/*
Two models:

1. VM_ACCOUNT:
    pre-account whole object

2. tmpfs default:
    account per page allocation
*/


/***************************************************************
 * 14. LIMITS
 ***************************************************************/

/*
Enforced via:

    sbinfo->max_blocks
    sbinfo->max_inodes
*/


/***************************************************************
 * 15. KEY FLAGS
 ***************************************************************/

/*
SHMEM_PAGEIN:
    page was swapped in

SHMEM_TRUNCATE:
    truncate in progress
*/


/***************************************************************
 * 16. FULL PAGE LIFECYCLE
 ***************************************************************/

/*
WRITE:
    user → page cache → dirty

RECLAIM:
    page → shmem_writepage → swap

READ / FAULT:
    swap → shmem_getpage → RAM
*/


/***************************************************************
 * 17. INTERVIEW MENTAL MODEL
 ***************************************************************/

/*
shmem = "filesystem on top of VM"

Instead of disk:
    uses swap

Instead of block mapping:
    uses page cache + swap vector
*/


/***************************************************************
 * 18. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/shmem.c implements tmpfs, a memory-backed filesystem where file data
resides in the page cache and is swapped out to swap space under memory
pressure, using a custom swap vector per inode.
*/


/***************************************************************
 * END
 ***************************************************************/
