/*
 * Linux 2.6.20 — mm/rmap.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - Background (CRITICAL for VM subsystem)
 *  - Reverse mapping (PA -> VA)
 *  - anon_vma vs file-backed (prio_tree)
 *  - page_referenced()
 *  - try_to_unmap() (reclaim core)
 *  - mapcount and rmap lifecycle
 *
 * Source: Linux 2.6.20 mm/rmap.c
 */


/***************************************************************
 * 0. BIG PICTURE (VERY IMPORTANT)
 ***************************************************************

/*
Normal mapping (forward mapping):

    VIRTUAL ADDRESS → PAGE TABLE → PHYSICAL PAGE


Reverse mapping (rmap):

    PHYSICAL PAGE → all VMAs → all PTEs


Why needed?

    When reclaiming memory:
        "I have a page, who is using it?"

    Needed for:
        - page reclaim (vmscan)
        - swapping
        - migration
        - unmapping
*/


/***************************************************************
 * 1. TWO TYPES OF RMAP
 ***************************************************************

/*
1. Anonymous pages (heap, stack):
    → anon_vma

2. File-backed pages:
    → address_space->i_mmap (prio_tree)
*/


/*
So:

    anon page  → anon_vma list
    file page  → prio_tree
*/


/***************************************************************
 * 2. anon_vma STRUCTURE
 ***************************************************************

struct anon_vma {
    spinlock_t lock;
    struct list_head head; // list of VMAs
};


/*
Each VMA points to anon_vma:

    vma->anon_vma

Each anon_vma tracks ALL VMAs sharing that anon memory.
*/


/***************************************************************
 * 3. anon_vma_prepare()
 ***************************************************************

/*
Ensures VMA has anon_vma

Steps:
    1. try to reuse existing anon_vma
    2. else allocate new one
    3. attach VMA to anon_vma->head list
*/


/*
Important locks:
    - mmap_sem (caller holds)
    - page_table_lock
    - anon_vma->lock
*/


/***************************************************************
 * 4. ADD / REMOVE RMAP
 ***************************************************************

/*
page_add_anon_rmap()
page_add_file_rmap()

page_remove_rmap()
*/


/*
mapcount:
    page->_mapcount

Meaning:
    how many PTEs map this page
*/


/*
Anonymous case:

    page->mapping = anon_vma (encoded)
    page->index   = offset in VMA
*/


/*
File case:

    page->mapping = address_space
*/


/***************************************************************
 * 5. page_referenced()
 ***************************************************************

/*
Goal:
    Check if page is recently used

Used by:
    page reclaim (LRU decision)
*/


/*
Flow:

1. check page flags (young / referenced)

2. if mapped:
       if anon:
            page_referenced_anon()
       else:
            page_referenced_file()
*/


/*
page_referenced_anon():
    iterate anon_vma->head
    check each VMA
*/


/*
page_referenced_file():
    iterate prio_tree (i_mmap)
*/


/*
page_referenced_one():

    find PTE
    clear young bit
    count reference
*/


/***************************************************************
 * 6. page_mkclean()
 ***************************************************************

/*
Goal:
    remove write permission from PTE
    clear dirty state

Used before writeback
*/


/*
Flow:

    for each mapping:
        clear PTE dirty/write
        mark page clean
*/


/***************************************************************
 * 7. CORE: try_to_unmap()
 ***************************************************************

/*
This is one of the MOST IMPORTANT functions in MM.

Goal:
    remove ALL mappings of a page
*/


/*
Entry:

    try_to_unmap(page)

    if anon:
        try_to_unmap_anon()
    else:
        try_to_unmap_file()
*/


/***************************************************************
 * 8. try_to_unmap_one()
 ***************************************************************

/*
Steps:

1. find address in VMA
2. locate PTE
3. check conditions:
       - VM_LOCKED → cannot unmap
       - recently used → skip

4. clear PTE

5. if dirty:
       set page dirty

6. update RSS

7. install swap entry (anon case)

8. page_remove_rmap()
*/


/*
IMPORTANT:

    this is where page → swap transition happens
*/


/***************************************************************
 * 9. FILE-BACKED UNMAP
 ***************************************************************

try_to_unmap_file()

/*
Flow:

1. iterate prio_tree
2. call try_to_unmap_one()

3. handle nonlinear VMAs (special case)
*/


/***************************************************************
 * 10. NONLINEAR VMA CASE
 ***************************************************************

/*
Problem:
    file offset != virtual address offset

Solution:
    scan clusters of VMAs
*/


/*
Function:
    try_to_unmap_cluster()

Scans chunk of VMA and unmaps pages
*/


/***************************************************************
 * 11. ANON UNMAP
 ***************************************************************

try_to_unmap_anon()

/*
Flow:

    iterate anon_vma->head
    call try_to_unmap_one()
*/


/***************************************************************
 * 12. RETURN VALUES
 ***************************************************************

/*
SWAP_SUCCESS → fully unmapped
SWAP_AGAIN   → retry later
SWAP_FAIL    → cannot unmap
*/


/***************************************************************
 * 13. LOCKING (VERY IMPORTANT)
 ***************************************************************

/*
Lock hierarchy (from file):

inode->i_mutex
  mmap_sem
    page lock
      i_mmap_lock
        anon_vma->lock
          pte_lock
*/


/*
Takeaway:
    rmap touches MANY subsystems → complex locking
*/


/***************************************************************
 * 14. FULL RECLAIM FLOW (CONNECT EVERYTHING)
 ***************************************************************

/*
Memory pressure (vmscan):

    pick victim page
        ↓
    page_referenced()
        ↓
    try_to_unmap()
        ↓
    if anon:
        swap out (page_io.c)

    if file:
        writeback (page-writeback.c)
*/


/***************************************************************
 * 15. INTERVIEW MENTAL MODEL
 ***************************************************************

/*
Question:
    "Given a physical page, how do we find all mappings?"

Answer:

    if PageAnon:
        anon_vma list

    else:
        prio_tree (i_mmap)
*/


/***************************************************************
 * 16. ONE-LINE SUMMARY
 ***************************************************************

/*
rmap.c implements reverse mapping (physical page → VMAs/PTEs), enabling
page reclaim, swapping, and unmapping by locating all virtual mappings
of a page.
*/


/***************************************************************
 * END
 ***************************************************************/

