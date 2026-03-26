================================================================================
FILE: migration.c — PAGE MIGRATION DEEP FLOW (ASCII IDE STYLE)
SOURCE: user file :contentReference[oaicite:0]{index=0}
================================================================================


SECTION 0: WHAT THIS FILE DOES (BIG PICTURE)
================================================================================

This file implements:

    "MOVE a page from one NUMA node → another"

It is used by:

    ✔ mempolicy (mbind, migrate_pages)
    ✔ NUMA balancing
    ✔ memory hotplug
    ✔ sys_move_pages()


--------------------------------------------------------------------------------
CORE IDEA:
--------------------------------------------------------------------------------

OLD PAGE (node A)
        ↓ copy + remap
NEW PAGE (node B)

→ all references updated
→ old page freed


================================================================================
SECTION 1: HIGH LEVEL FLOW
================================================================================

migrate_pages()
    |
    |-- for each page:
    |       unmap_and_move()
    |
    |-- retry logic (up to 10 passes)
    |
    |-- putback_lru_pages()


================================================================================
SECTION 2: PAGE MIGRATION PIPELINE
================================================================================

This is the MOST IMPORTANT FLOW.


--------------------------------------------------------------------------------
STEP 1: PREPARE
--------------------------------------------------------------------------------

migrate_prep()

    → lru_add_drain_all()

WHY?

    ensure pages can be isolated from LRU


--------------------------------------------------------------------------------
STEP 2: ISOLATE PAGE
--------------------------------------------------------------------------------

isolate_lru_page(page)

    → remove from LRU list
    → increase refcount
    → add to migration list


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

LRU LIST:
    [page1] [page2] [page3]

            ↓ isolate

MIGRATION LIST:
            [page2]


================================================================================
SECTION 3: CORE FUNCTION — unmap_and_move()
================================================================================

This is the HEART of migration.


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

unmap_and_move():
    |
    |-- allocate new page (target node)
    |
    |-- lock old page
    |
    |-- wait for writeback (if needed)
    |
    |-- try_to_unmap(page)
    |
    |-- if unmapped:
    |       move_to_new_page()
    |
    |-- else:
    |       restore state


--------------------------------------------------------------------------------
KEY IDEA:
--------------------------------------------------------------------------------

You CANNOT migrate while page is mapped


================================================================================
SECTION 4: UNMAPPING (VERY IMPORTANT)
================================================================================

try_to_unmap(page)

    → removes all PTEs pointing to page
    → replaces with migration entries


--------------------------------------------------------------------------------
WHAT IS A MIGRATION ENTRY?
--------------------------------------------------------------------------------

Instead of:

    PTE → physical page

we temporarily have:

    PTE → "migration entry"


--------------------------------------------------------------------------------
WHY?
--------------------------------------------------------------------------------

CPU accessing page → must wait for migration


================================================================================
SECTION 5: MIGRATION ENTRY WAIT
================================================================================

migration_entry_wait()

    if fault hits migration entry:

        → wait_on_page_locked(page)

        → retry fault


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

CPU access →
    sees migration entry →
        sleep →
            wake →
                retry


================================================================================
SECTION 6: MOVE TO NEW PAGE
================================================================================

move_to_new_page()

    |
    |-- prepare newpage metadata
    |
    |-- migrate_page() OR filesystem-specific handler
    |
    |-- remove migration PTEs


--------------------------------------------------------------------------------
IMPORTANT:
--------------------------------------------------------------------------------

Different paths:

    Anonymous pages
    File-backed pages
    Buffer pages


================================================================================
SECTION 7: CORE COPY LOGIC
================================================================================

migrate_page_copy()

    → copy_highpage()
    → copy flags:
        - dirty
        - referenced
        - uptodate
        - active


--------------------------------------------------------------------------------
AFTER COPY:
--------------------------------------------------------------------------------

old page:
    → cleared state
    → mapping removed


================================================================================
SECTION 8: PAGE TABLE FIXUP
================================================================================

remove_migration_ptes()

    → replace migration entry with:

        PTE → newpage


--------------------------------------------------------------------------------
IMPORTANT:
--------------------------------------------------------------------------------

THIS is when mapping becomes valid again


================================================================================
SECTION 9: ADDRESS SPACE UPDATE
================================================================================

migrate_page_move_mapping()

    updates:

        mapping->page_tree (radix tree)


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

radix_tree:
    old_page → replaced with new_page


================================================================================
SECTION 10: ERROR HANDLING PATHS
================================================================================

Common failures:

    -EAGAIN → retry
    -ENOMEM → abort
    -EBUSY → skip
    -EIO → permanent failure


--------------------------------------------------------------------------------
RETRY LOOP:
--------------------------------------------------------------------------------

migrate_pages():

    up to 10 passes


================================================================================
SECTION 11: LRU RESTORATION
================================================================================

putback_lru_pages()

    → put pages back if migration failed


--------------------------------------------------------------------------------
SUCCESS CASE:
--------------------------------------------------------------------------------

old page → freed  
new page → added to LRU


================================================================================
SECTION 12: FILESYSTEM INTERACTION
================================================================================

If file-backed:

    mapping->a_ops->migratepage()


Else:

    fallback_migrate_page()


--------------------------------------------------------------------------------
DIRTY PAGE CASE:
--------------------------------------------------------------------------------

writeout()

    → flush page to disk before migration


================================================================================
SECTION 13: NUMA-SPECIFIC ENTRY POINT
================================================================================

Used by mempolicy:

    migrate_to_node()
    do_migrate_pages()


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

mempolicy →
    collect pages →
        isolate →
            migrate_pages()


================================================================================
SECTION 14: USERSPACE API
================================================================================

sys_move_pages()

    user specifies:

        VA → target node


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

user →
    syscall →
        do_move_pages()
            |
            → follow_page()
            → isolate
            → migrate


================================================================================
SECTION 15: IMPORTANT KERNEL CONCEPTS USED
================================================================================

This file touches MANY subsystems:

    ✔ LRU (page reclaim)
    ✔ rmap (reverse mapping)
    ✔ page tables
    ✔ radix tree (address_space)
    ✔ writeback (dirty pages)
    ✔ swap
    ✔ NUMA


================================================================================
SECTION 16: COMPLETE END-TO-END FLOW
================================================================================

--------------------------------------------------------------------------------
MIGRATION FLOW
--------------------------------------------------------------------------------

[1] isolate_lru_page()
        ↓
[2] unmap_and_move()
        ↓
[3] try_to_unmap()
        ↓
[4] insert migration entry
        ↓
[5] allocate new page
        ↓
[6] copy data
        ↓
[7] update mapping (radix tree)
        ↓
[8] replace PTEs
        ↓
[9] free old page


--------------------------------------------------------------------------------
ASCII MASTER FLOW:
--------------------------------------------------------------------------------

OLD PAGE (node A)
    |
    |-- unmap (remove PTEs)
    |
    |-- migration entry inserted
    |
    |-- allocate NEW PAGE (node B)
    |
    |-- copy contents
    |
    |-- update page tables
    |
    |-- free old page

→ DONE


================================================================================
SECTION 17: CONNECTION TO mempolicy.c (IMPORTANT FOR YOU)
================================================================================

From your previous file:

    mempolicy.c

calls:

    migrate_pages()


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

mbind()
    |
    → check_range()   (walk page tables)
    |
    → collect pages
    |
    → migrate_pages()   ← THIS FILE


================================================================================
SECTION 18: INTERVIEW INSIGHTS (VERY IMPORTANT)
================================================================================

1) Migration requires:
       → unmapping all PTEs

2) Uses migration entries:
       → temporary placeholder

3) Safe concurrency:
       → page lock + refcount + rmap

4) Works with:
       → anonymous + file-backed pages

5) Retry-based system:
       → handles transient failures


================================================================================
SECTION 19: ONE-LINE SUMMARY
================================================================================

    migration.c safely moves pages across NUMA nodes by unmapping them,
    copying data, updating mappings, and restoring page table entries.


================================================================================
END
================================================================================
