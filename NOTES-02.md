# kvm rmap (driver/kvm/mmu.c)
================================================================================
                     WHY RMAP IS NEEDED IN KVM SHADOW MMU
================================================================================

CORE PROBLEM
------------

Shadow paging maintains TWO VIEWS of MEMORY:

    Guest View                    Host / Hardware View
    ----------                    ---------------------
    Guest Page Tables      <--->  Shadow Page Tables
    (Guest-controlled)            (KVM-controlled)

Hardware walks ONLY the SHADOW PAGE TABLES.

================================================================================
1. THE FUNDAMENTAL MISMATCH
================================================================================

Guest page tables are FORWARD mappings:

    GVA ---> GPA

Shadow page tables are ALSO forward mappings:

    GVA ---> HPA

BUT KVM OFTEN NEEDS THE REVERSE QUESTION:

    "Given a GPA (or guest page), WHICH shadow PTEs point to it?"

WITHOUT rmap, THIS QUESTION IS IMPOSSIBLE TO ANSWER EFFICIENTLY.

================================================================================
2. WHY THIS QUESTION MATTERS
================================================================================

There are FOUR CRITICAL EVENTS where KVM MUST answer:

    Q1. Guest writes to memory
    Q2. Guest writes to its own page tables
    Q3. KVM revokes write permission
    Q4. Memory slot is removed / invalidated

ALL FOUR REQUIRE:
    "Find all shadow PTEs mapping this guest page"

================================================================================
3. WHAT HAPPENS WITHOUT RMAP (FAILURE CASE)
================================================================================

ASSUME:
-------
Guest page GFN=0x100 is mapped MANY TIMES:

    GVA1 --> GFN 0x100
    GVA2 --> GFN 0x100
    GVA3 --> GFN 0x100

Shadow PTs look like:

    SPTE_A --> HPA(0x100)
    SPTE_B --> HPA(0x100)
    SPTE_C --> HPA(0x100)

NO DATA STRUCTURE RECORDS THIS RELATIONSHIP.

----------------------------------------------------------------
CASE A: GUEST WRITES TO MEMORY
----------------------------------------------------------------

Guest executes:
    MOV [GVA1], RAX

KVM MUST:
    - Clear WR bit in ALL shadow mappings
    - So future writes trap again

WITHOUT RMAP:
    - You know GFN=0x100 was written
    - You do NOT know SPTE_A / B / C
    - You would have to SCAN EVERY SHADOW PAGE TABLE

THIS IS O(N) OVER ALL SHADOW PAGES → UNACCEPTABLE.

----------------------------------------------------------------
CASE B: GUEST WRITES TO ITS PAGE TABLE
----------------------------------------------------------------

Guest modifies its PTE for GFN=0x100.

KVM MUST:
    - Invalidate ALL shadow mappings
    - Or shadow will lie to hardware

WITHOUT RMAP:
    - No way to find all SPTEs
    - Guest sees stale mappings
    - MEMORY CORRUPTION

----------------------------------------------------------------
CASE C: WRITE PROTECTION (DIRTY TRACKING)
----------------------------------------------------------------

KVM wants:
    - First write to page traps
    - After that, allow writes

Steps:
    1. Write-protect all shadow PTEs
    2. Trap on write
    3. Mark page dirty
    4. Remove write-protect

WITHOUT RMAP:
    - Cannot step (1)
    - Cannot find all PTEs
    - Dirty tracking FAILS

----------------------------------------------------------------
CASE D: MEMORY SLOT REMOVAL
----------------------------------------------------------------

Hot-unplug memory slot.

KVM MUST:
    - Remove all shadow mappings
    - Before host frees memory

WITHOUT RMAP:
    - Shadow PTEs still point to freed memory
    - USE-AFTER-FREE
    - HOST CRASH

================================================================================
4. WHAT RMAP PROVIDES (THE SOLUTION)
================================================================================

Reverse Mapping answers:

    GPA / GFN ---> list of shadow PTEs

Stored like this:

    struct page (guest page)
        |
        v
    page->private
        |
        +--> SPTE
        |
        +--> rmap_desc --> SPTE, SPTE, SPTE, ...

================================================================================
5. HOW RMAP IS USED IN PRACTICE
================================================================================

----------------------------------------------------------------
WRITE TO MEMORY
----------------------------------------------------------------

Guest writes GFN=0x100

KVM:
    page = gfn_to_page(0x100)
    for each spte in page->private:
        clear WR bit
        flush TLB

----------------------------------------------------------------
PAGE TABLE WRITE
----------------------------------------------------------------

Guest modifies PT page

KVM:
    find shadow pages for that PT GFN
    rmap_remove all leaf SPTEs
    unlink child shadow pages

----------------------------------------------------------------
WRITE PROTECTION
----------------------------------------------------------------

rmap_write_protect(gfn):
    while page->private:
        spte = one mapping
        clear WR
        rmap_remove(spte)

----------------------------------------------------------------
SLOT REMOVAL
----------------------------------------------------------------

slot_remove_write_access(slot):
    for each shadow page:
        if slot_bitmap includes slot:
            remove writable mappings via rmap

================================================================================
6. WHY page->private IS USED
================================================================================

WHY NOT A HASH MAP?

- page already represents a physical frame
- page lifetime == guest page lifetime
- avoids extra allocation
- extremely fast access
- memory locality

Small optimization:
    1 mapping → direct pointer
    many mappings → rmap_desc chain

================================================================================
7. WHY THIS DISAPPEARS WITH EPT/NPT
================================================================================

EPT/NPT = TWO-DIMENSIONAL PAGING

Hardware handles:

    GVA --> GPA --> HPA

No shadow page tables.

Therefore:

    - Guest page tables are real
    - KVM does NOT mirror them
    - No need to invalidate shadow PTEs
    - No need to track reverse mappings

DIRTY BIT is provided by hardware.

RMAP becomes UNNECESSARY.

================================================================================
8. FINAL MENTAL MODEL
================================================================================

WITHOUT RMAP:
--------------
Shadow paging CANNOT be correct.

WITH RMAP:
-----------
Shadow paging is:
    - Correct
    - Performant
    - Maintainable

RMAP is the "back-pointer" that makes
forward-only page tables manageable.

================================================================================
END
================================================================================

