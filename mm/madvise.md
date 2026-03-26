================================================================================
FILE: linux_mm_madvise_background_and_flow.txt
TOPIC: madvise.c - advisory memory behavior hints
SOURCE: user-provided code :contentReference[oaicite:0]{index=0}
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHAT THIS FILE DOES
================================================================================

This file implements the:

    madvise() system call

Purpose:

    allow user-space to give "hints" to the kernel about how it will
    use memory.

Key idea:

    This is NOT mandatory behavior.
    It is only a hint (advisory).

The kernel may:

    follow it
    partially follow it
    ignore it

depending on system state.



SECTION 2: BIG PICTURE
================================================================================

madvise() operates on:

    a range of virtual memory [start, start + len)

and applies:

    a "behavior hint"

to the VMAs covering that range.

So the flow is:

    user → sys_madvise()
        → iterate VMAs
            → apply behavior per VMA chunk



SECTION 3: TYPES OF ADVICE (CORE MODES)
================================================================================

Main behaviors supported:

    MADV_NORMAL
        default behavior (reset hints)

    MADV_RANDOM
        expect random access → minimal readahead

    MADV_SEQUENTIAL
        expect sequential access → aggressive readahead

    MADV_WILLNEED
        prefetch data into memory

    MADV_DONTNEED
        drop pages (free them)

    MADV_REMOVE
        free backing store (hole punching)

    MADV_DONTFORK / MADV_DOFORK
        control inheritance across fork()

So this file maps:

    "user intent"
        →
    "VM flags / reclaim / I/O behavior"



SECTION 4: CORE DISPATCH FUNCTION
================================================================================

Main decision point:

    madvise_vma()

This does:

    switch(behavior):
        → madvise_behavior()
        → madvise_willneed()
        → madvise_dontneed()
        → madvise_remove()

So each behavior has its own specialized logic.



SECTION 5: madvise_behavior() (FLAG CHANGES)
================================================================================

Handles:

    MADV_NORMAL
    MADV_RANDOM
    MADV_SEQUENTIAL
    MADV_DONTFORK
    MADV_DOFORK

What it does:

    modifies vma->vm_flags

Examples:

    MADV_RANDOM
        → set VM_RAND_READ
        → clear VM_SEQ_READ

    MADV_SEQUENTIAL
        → set VM_SEQ_READ
        → clear VM_RAND_READ

    MADV_DONTFORK
        → set VM_DONTCOPY

    MADV_DOFORK
        → clear VM_DONTCOPY


================================================================================
IMPORTANT: THIS DOES NOT TOUCH PAGE TABLES
================================================================================

It only updates:

    VMA metadata (vm_flags)

Effect is indirect:

    future behavior (readahead, reclaim) changes
    but no immediate page-level changes occur



SECTION 6: VMA SPLITTING LOGIC
================================================================================

Key pattern:

    vma_merge(...)
    OR
    split_vma(...)

Why?

Because:

    madvise() may apply to only part of a VMA

Example:

    [----- VMA -----]
          [--- range ---]

Kernel must split:

    [--][---][--]
     A   B    C

Apply new behavior only to B.

So:

    try merge first (fast path)
    else split VMA into pieces

This pattern appears across:

    madvise
    mlock
    mprotect



SECTION 7: madvise_willneed()
================================================================================

Purpose:

    "I will need these pages soon → prefetch them"

Code:

    force_page_cache_readahead()

Flow:

    convert virtual range → file offsets
    trigger readahead on file mapping

Important:

    does NOT block
    does NOT guarantee pages are loaded

It just:

    schedules I/O


================================================================================
KEY IDEA:
================================================================================

    This is like manual readahead hinting

    (user says: "start warming cache")



SECTION 8: madvise_dontneed()
================================================================================

Purpose:

    "I don't need these pages anymore → drop them"

Code:

    zap_page_range()

Effect:

    unmaps pages from page tables
    frees memory (eventually)
    discards data


================================================================================
CRITICAL DETAIL:
================================================================================

    Dirty pages are NOT written out
    they are simply discarded

This is intentional:

    app is saying:
        "I don't care about this data anymore"


================================================================================
NONLINEAR SPECIAL CASE:
================================================================================

If:

    VM_NONLINEAR

then:

    zap_page_range(..., &details)

because nonlinear mappings need special handling for page lookup.



SECTION 9: madvise_remove()
================================================================================

Purpose:

    "remove backing store" (hole punching)

Works only for:

    tmpfs / shm (shared memory)

Flow:

    compute file offsets
    call:

        vmtruncate_range()

Effect:

    underlying storage is freed


================================================================================
IMPORTANT:
================================================================================

This is NOT just dropping memory.

This modifies:

    the file itself



SECTION 10: sys_madvise() - ENTRY POINT
================================================================================

Function:

    sys_madvise(start, len, behavior)

Main responsibilities:

    validate arguments
    iterate VMAs
    apply advice piecewise


================================================================================
FLOW:
================================================================================

    down_write(mmap_sem)

    align start and len
    validate range

    find first VMA

    loop:
        adjust start if in hole
        compute [start, tmp] inside VMA
        apply madvise_vma()
        move to next VMA

    up_write(mmap_sem)



SECTION 11: HANDLING HOLES (IMPORTANT)
================================================================================

Unlike:

    mlock
    mmap

madvise does:

    ignore unmapped regions

but:

    returns -ENOMEM at the end

So behavior is:

    best-effort over mapped areas



SECTION 12: WHY mmap_sem WRITE LOCK?
================================================================================

Because madvise may:

    split VMAs
    merge VMAs
    modify vm_flags

These are structural changes → require write lock.



SECTION 13: RSS AND PAGE TABLE EFFECTS
================================================================================

Behavior types differ:

1) FLAG-only (madvise_behavior)
    → no RSS change
    → no page table change

2) WILLNEED
    → may increase RSS later (prefetch)
    → no immediate change

3) DONTNEED
    → immediate unmap
    → RSS decreases

4) REMOVE
    → underlying storage removed
    → pages invalidated



SECTION 14: CONNECTION TO OTHER FILES YOU STUDIED
================================================================================

This file connects strongly with:

--------------------------------------------------------------------------------
mmap.c
--------------------------------------------------------------------------------
    manages VMAs
    madvise uses:
        split_vma()
        vma_merge()

--------------------------------------------------------------------------------
fremap.c
--------------------------------------------------------------------------------
    nonlinear mappings
    madvise_dontneed has special handling for:
        VM_NONLINEAR

--------------------------------------------------------------------------------
migration.c
--------------------------------------------------------------------------------
    page movement
    madvise indirectly influences reclaim pressure

--------------------------------------------------------------------------------
mempool.c
--------------------------------------------------------------------------------
    unrelated directly, but used under memory pressure triggered by madvise

--------------------------------------------------------------------------------


SECTION 15: ASCII FLOW
================================================================================

User:
    madvise(start, len, behavior)
        |
        v
sys_madvise()
        |
        |-- validate args
        |-- down_write(mmap_sem)
        |
        |-- find VMA covering start
        |
        |-- LOOP over VMAs:
        |       |
        |       |-- compute subrange [start, tmp]
        |       |
        |       |-- madvise_vma()
        |               |
        |               |-- behavior switch:
        |                       |
        |                       |-- madvise_behavior()
        |                       |-- madvise_willneed()
        |                       |-- madvise_dontneed()
        |                       |-- madvise_remove()
        |
        |-- move to next VMA
        |
        |-- up_write(mmap_sem)
        v
return



SECTION 16: DESIGN INSIGHT
================================================================================

madvise is:

    a control plane API

NOT:

    a data plane API

Meaning:

    it changes policies and hints
    it rarely directly manipulates data (except DONTNEED/REMOVE)

Most effects are:

    indirect
    lazy
    heuristic-driven



SECTION 17: MOST IMPORTANT TAKEAWAYS
================================================================================

1) madvise operates at VMA level
    not raw page-table level (except DONTNEED)

2) many operations require VMA split/merge
    because advice is range-specific

3) MADV_DONTNEED is the only one that aggressively:
    → unmaps pages immediately

4) MADV_WILLNEED:
    → triggers readahead
    → does not guarantee population

5) MADV_RANDOM / SEQUENTIAL:
    → influence kernel heuristics (readahead + reclaim)

6) MADV_REMOVE:
    → modifies underlying storage (not just memory)

7) VM_NONLINEAR requires special handling
    → ties back to fremap.c



SECTION 18: ONE-LINE SUMMARY
================================================================================

    madvise.c implements advisory memory hints that modify VMA behavior,
    influence paging policies, and optionally drop or prefetch pages,
    without changing program correctness.


================================================================================
END OF FILE
================================================================================
