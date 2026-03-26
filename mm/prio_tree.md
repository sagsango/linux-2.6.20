/*
 * Linux 2.6.20 — mm/prio_tree.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - Background (VERY IMPORTANT for mmap/file-backed VMAs)
 *  - Why prio_tree exists
 *  - Data structure (interval + radix hybrid)
 *  - i_mmap relationship
 *  - vm_set trick (very important subtlety)
 *  - Insert / remove / iteration flows
 *
 * Source: Linux 2.6.20 mm/prio_tree.c
 */


/***************************************************************
 * 0. BIG PICTURE (WHY THIS FILE EXISTS)
 ***************************************************************

/*
Problem:
    Given a file-backed page (or range of pages), we need to find:

        "Which VMAs map this file offset?"

Where this is used:
    - page writeback (writeback wants to find mappings)
    - page invalidation
    - page migration
    - unmap (zap PTEs)


Context:

    struct address_space
        -> i_mmap   (prio_tree_root)

    This tree stores ALL VMAs mapping a file.


Goal:
    Efficiently query overlapping ranges:

        file offset range  <->  VMAs mapping that range
*/


/***************************************************************
 * 1. WHAT IS THIS DATA STRUCTURE?
 ***************************************************************

/*
This is a RADIX PRIORITY SEARCH TREE.

It combines:

    (1) Radix tree (for indexing)
    (2) Interval tree (for range overlap queries)


Each VMA is represented as an interval:

    [radix_index, heap_index]

Where:

    radix_index = vm_pgoff
    heap_index  = vm_pgoff + (#pages - 1)
*/


/*
Macros from code:

    RADIX_INDEX(vma)  = vma->vm_pgoff

    VMA_SIZE(vma)     = (vm_end - vm_start) >> PAGE_SHIFT

    HEAP_INDEX(vma)   = vm_pgoff + (VMA_SIZE - 1)
*/


/***************************************************************
 * 2. WHAT IS STORED IN THE TREE?
 ***************************************************************

/*
Tree stores VMAs mapping a FILE.

Important distinction:

    This is NOT process address space tree.

    This is FILE → VMA mapping.

Example:

    file foo.txt mapped by multiple processes:

        process A → mmap(foo)
        process B → mmap(foo)
        process C → mmap(foo)

All these VMAs go into:

    mapping->i_mmap (prio_tree)
*/


/***************************************************************
 * 3. WHY NOT JUST USE A LIST?
 ***************************************************************

/*
Because we need FAST queries like:

    "find all VMAs that overlap file offset X"

List → O(n)

Prio tree → O(log n + k)
*/


/***************************************************************
 * 4. THE HARD PROBLEM: DUPLICATE RANGES
 ***************************************************************

/*
Multiple VMAs can map EXACT SAME range.

Example:

    fork() duplicates mappings

So many VMAs may have identical:

    [radix_index, heap_index]


Tree stores only ONE node per unique range.
Others are stored in a list (vm_set).
*/


/***************************************************************
 * 5. vm_set DESIGN (VERY IMPORTANT)
 ***************************************************************

/*
Each VMA has:

    vma->shared.vm_set

Fields:

    parent  → indicates tree node
    head    → head of list for duplicates
    list    → linked list for duplicates
*/


/*
Classification:

1. TREE NODE:
    parent != NULL

2. LIST HEAD:
    parent == NULL AND head != NULL

3. LIST MEMBER:
    parent == NULL AND head == NULL
*/


/*
ASCII from source:

        Tree node (R)
           |
           v
        vm_set.head → H → I → J → ...

Meaning:
    R is tree node
    H is head of duplicates
    I, J, ... are additional VMAs with same range
*/


/***************************************************************
 * 6. WHY vm_flags CANNOT BE USED
 ***************************************************************

/*
Critical subtlety from comments:

We cannot mark node/list-head using vm_flags.

Why?
    Because different VMAs may be protected by different mmap_sem locks.

Example:
    Thread A modifies VMA R
    Thread B modifies VMA H

We cannot safely update H->vm_flags without holding its mmap_sem.

So instead:
    use shared.vm_set.parent as indicator
*/


/***************************************************************
 * 7. INSERT FLOW
 ***************************************************************

Function:
    vma_prio_tree_insert(vma, root)


Flow:

    ptr = raw_prio_tree_insert(root, node)

    if ptr != node:
        // duplicate range
        old = existing VMA
        vma_prio_tree_add(vma, old)
*/


/*
Meaning:

Case 1: unique range
    → becomes tree node

Case 2: duplicate range
    → attach to existing vm_set list
*/


/***************************************************************
 * 8. vma_prio_tree_add()
 ***************************************************************

/*
Adds VMA to vm_set list of duplicates

Cases:

1. old is NOT tree node:
    → simple list_add

2. old has head:
    → append to head's list

3. old has no head yet:
    → create head relationship
*/


/*
Important invariants:

    All VMAs with identical range are grouped
    Only one node exists in tree
*/


/***************************************************************
 * 9. REMOVE FLOW
 ***************************************************************

Function:
    vma_prio_tree_remove(vma, root)


Cases:

1. Not part of vm_set:
    → remove directly from tree

2. Is tree node with duplicates:
    → replace with new head

3. Is list head:
    → update list

4. Is list member:
    → remove from list
*/


/*
Key complexity:

When removing a tree node with duplicates:

    head replaces node in tree

This requires careful pointer manipulation.
*/


/***************************************************************
 * 10. ITERATION (QUERY)
 ***************************************************************

Function:
    vma_prio_tree_next(vma, iter)


Goal:
    Find all VMAs overlapping a given file page range
*/


/*
Flow:

if first call:
    ptr = prio_tree_next(iter)
    return corresponding VMA

if current VMA has duplicates:
    return next duplicate from vm_set list

else:
    move to next tree node via prio_tree_next()
*/


/*
Important optimization:

    prefetch(next->...)

Used to reduce cache misses when walking tree/list
*/


/***************************************************************
 * 11. HOW THIS CONNECTS TO MEMORY MANAGEMENT
 ***************************************************************

/*
Example: page writeback

    page belongs to file mapping

    want:
        all VMAs mapping this page

    → use i_mmap prio_tree


Example: unmap (zap page)

    find all processes mapping this page

    → prio_tree traversal
*/


/***************************************************************
 * 12. RELATION TO rmap (REVERSE MAPPING)
 ***************************************************************

/*
Anonymous pages:
    → use rmap (anon_vma)

File-backed pages:
    → use prio_tree (i_mmap)

So:

    rmap handles anonymous memory
    prio_tree handles file-backed memory
*/


/***************************************************************
 * 13. PERFORMANCE INSIGHTS
 ***************************************************************

/*
1. O(log n) lookup
2. Efficient overlap queries
3. Avoid duplicate tree nodes
4. Cache-friendly traversal (prefetch)
*/


/***************************************************************
 * 14. INTERVIEW MENTAL MODEL
 ***************************************************************

/*
Question:
    "How does kernel find all VMAs mapping a file page?"

Answer:

    For file-backed pages:
        mapping->i_mmap (prio_tree)

    For anonymous pages:
        anon_vma (rmap)
*/


/***************************************************************
 * 15. ONE-LINE SUMMARY
 ***************************************************************

/*
prio_tree.c implements a radix-based interval tree used by
address_space->i_mmap to efficiently find all VMAs that map a given
file offset range, with special handling for duplicate ranges via
vm_set lists.
*/


/***************************************************************
 * END
 ***************************************************************/

