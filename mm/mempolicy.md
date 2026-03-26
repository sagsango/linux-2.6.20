================================================================================
FILE: numa_mempolicy.c — DEEP EXECUTION FLOW (ASCII IDE STYLE)
SOURCE: user file :contentReference[oaicite:0]{index=0}
================================================================================


SECTION 0: WHAT THIS FILE DOES (INTUITION)
================================================================================

This file controls:

    "WHERE memory is allocated in a NUMA system"

It does NOT allocate memory itself.
It decides:

    → which NUMA node to allocate from


Think of it like:

    memory.c  = HOW memory is handled
    mempolicy.c = WHERE memory comes from


================================================================================
SECTION 1: CORE ABSTRACTION
================================================================================

struct mempolicy {
    policy type:
        MPOL_DEFAULT
        MPOL_BIND
        MPOL_INTERLEAVE
        MPOL_PREFERRED

    + data:
        nodes / zonelist / preferred node
}

Two scopes:

    1) PROCESS policy (current->mempolicy)
    2) VMA policy     (vma->vm_policy)

IMPORTANT:

    VMA policy > Process policy


================================================================================
SECTION 2: POLICY TYPES (VERY IMPORTANT)
================================================================================

--------------------------------------------------------------------------------
1) MPOL_DEFAULT
--------------------------------------------------------------------------------
    → local allocation (NUMA node of CPU)

--------------------------------------------------------------------------------
2) MPOL_BIND
--------------------------------------------------------------------------------
    → STRICT nodes only
    → no fallback

--------------------------------------------------------------------------------
3) MPOL_PREFERRED
--------------------------------------------------------------------------------
    → try one node first
    → fallback allowed

--------------------------------------------------------------------------------
4) MPOL_INTERLEAVE
--------------------------------------------------------------------------------
    → round-robin across nodes


================================================================================
SECTION 3: WHERE POLICY IS USED (CRITICAL CONNECTION)
================================================================================

Main entry points:

    alloc_pages_current()
    alloc_page_vma()

These are called from:

    memory.c (page fault path, GUP, etc)


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

Page fault →
    memory.c →
        alloc_page_vma() →
            mempolicy.c decides node →
                __alloc_pages()


================================================================================
SECTION 4: POLICY CREATION FLOW
================================================================================

User space syscall:

    sys_set_mempolicy()
        |
        → do_set_mempolicy()
                |
                → mpol_new()
                        |
                        → initialize struct mempolicy


--------------------------------------------------------------------------------
VALIDATION:
--------------------------------------------------------------------------------

mpol_check_policy()

    ensures:
        - nodes valid
        - nodes not empty (for bind/interleave)
        - nodes ⊆ online nodes


================================================================================
SECTION 5: ALLOCATION DECISION FLOW
================================================================================

This is the MOST IMPORTANT PART.

--------------------------------------------------------------------------------
alloc_page_vma()
--------------------------------------------------------------------------------

1) get_vma_policy()

        if VMA has policy → use it
        else → process policy
        else → default

2) switch(policy):

    INTERLEAVE:
        → interleave_nid()
        → alloc_page_interleave()

    BIND / PREFERRED / DEFAULT:
        → zonelist_policy()
        → __alloc_pages()


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

alloc_page_vma()
    |
    |-- get policy
    |
    |-- if INTERLEAVE:
    |       node = interleave_nid()
    |       alloc from that node
    |
    |-- else:
    |       zonelist = zonelist_policy()
    |       alloc from zonelist


================================================================================
SECTION 6: INTERLEAVE LOGIC
================================================================================

Two modes:

--------------------------------------------------------------------------------
PROCESS INTERLEAVE
--------------------------------------------------------------------------------

interleave_nodes()

    uses:

        current->il_next

    round robin:
        node0 → node1 → node2 → ...


--------------------------------------------------------------------------------
VMA INTERLEAVE
--------------------------------------------------------------------------------

interleave_nid()

    based on:

        offset inside mapping


--------------------------------------------------------------------------------
KEY IDEA:
--------------------------------------------------------------------------------

Process → time-based interleave  
VMA     → address-based interleave


================================================================================
SECTION 7: BIND POLICY (STRICT CONTROL)
================================================================================

bind_zonelist()

    builds:

        custom zonelist of allowed nodes


Allocation:

    ONLY from these nodes


--------------------------------------------------------------------------------
IMPORTANT:
--------------------------------------------------------------------------------

No fallback → can trigger OOM faster


================================================================================
SECTION 8: POLICY APPLICATION TO MEMORY
================================================================================

System call:

    sys_mbind()

Flow:

    do_mbind()
        |
        → check_range()
                |
                → walk page tables (!!)
        |
        → mbind_range()
                |
                → split VMA if needed
                → apply policy


--------------------------------------------------------------------------------
IMPORTANT:
--------------------------------------------------------------------------------

This is NOT just metadata change:

    it can MIGRATE pages


================================================================================
SECTION 9: PAGE MIGRATION (ADVANCED)
================================================================================

Triggered via:

    MPOL_MF_MOVE / MPOL_MF_MOVE_ALL


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

check_range()
    |
    → walk page tables (pgd → pte)
    |
    → collect pages
    |
    → migrate_pages()


--------------------------------------------------------------------------------
KEY FUNCTION:
--------------------------------------------------------------------------------

migrate_to_node()

    move pages:
        source node → destination node


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

before:
    page → node 0

after:
    page → node 1


================================================================================
SECTION 10: PAGE TABLE WALK (VERY IMPORTANT)
================================================================================

This file also walks page tables (like memory.c!)

Functions:

    check_pgd_range()
        → check_pud_range()
            → check_pmd_range()
                → check_pte_range()


--------------------------------------------------------------------------------
INSIDE check_pte_range():
--------------------------------------------------------------------------------

for each PTE:

    if present:
        page = vm_normal_page()

        if valid:
            check node
            collect / migrate / stats


--------------------------------------------------------------------------------
IMPORTANT CONNECTION:
--------------------------------------------------------------------------------

Uses same primitives as:

    memory.c
    rmap
    migration


================================================================================
SECTION 11: get_user_pages() CONNECTION
================================================================================

lookup_node()

    uses:

        get_user_pages()

to:

    find page → get node


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

VA →
    GUP →
        struct page →
            page_to_nid()


================================================================================
SECTION 12: PROCESS POLICY VS VMA POLICY
================================================================================

--------------------------------------------------------------------------------
get_vma_policy()
--------------------------------------------------------------------------------

priority:

    if VMA policy exists → use it
    else if process policy → use it
    else → default


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

access VA →
    find VMA →
        if vma->policy:
            use it
        else:
            use process policy


================================================================================
SECTION 13: cpuset INTERACTION (VERY IMPORTANT)
================================================================================

Policies are constrained by:

    cpuset_mems_allowed


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

cpuset_update_task_memory_state()

    ensures:

        policy nodes ⊆ allowed nodes


--------------------------------------------------------------------------------
IMPORTANT:
--------------------------------------------------------------------------------

Even if user sets:

    bind to node 3

if cpuset disallows:

    → allocation won't happen there


================================================================================
SECTION 14: SHARED POLICY (FILES / SHM)
================================================================================

Shared memory uses:

    RB-tree of policies


--------------------------------------------------------------------------------
STRUCT:
--------------------------------------------------------------------------------

shared_policy
    → RB tree of sp_node


--------------------------------------------------------------------------------
WHY:
--------------------------------------------------------------------------------

Policies persist even if:

    no process maps memory


================================================================================
SECTION 15: REBINDING (DYNAMIC SYSTEM CHANGE)
================================================================================

When cpuset changes:

    mpol_rebind_policy()

updates:

    node mappings


--------------------------------------------------------------------------------
EXAMPLE:
--------------------------------------------------------------------------------

node 2 removed →

    remap policy nodes


================================================================================
SECTION 16: END-TO-END FLOW
================================================================================

--------------------------------------------------------------------------------
PAGE FAULT + NUMA POLICY
--------------------------------------------------------------------------------

CPU access →
    page fault →
        memory.c →
            alloc_page_vma() →
                mempolicy decides node →
                    __alloc_pages()


--------------------------------------------------------------------------------
MBIND + MIGRATION
--------------------------------------------------------------------------------

user → mbind()
    |
    → check_range() (walk page tables)
    |
    → migrate_pages()


--------------------------------------------------------------------------------
PROCESS POLICY SET
--------------------------------------------------------------------------------

user → set_mempolicy()
    |
    → mpol_new()
    |
    → attach to current->mempolicy


================================================================================
SECTION 17: KEY INSIGHTS (INTERVIEW GOLD)
================================================================================

1) mempolicy does NOT allocate memory  
   → it guides allocation

2) integrated deeply with:
       - page fault path
       - allocator (__alloc_pages)

3) policy precedence:
       VMA > process > default

4) supports:
       - placement
       - migration
       - interleaving

5) NUMA = performance optimization, not correctness


================================================================================
SECTION 18: ONE-LINE SUMMARY
================================================================================

    mempolicy.c controls NUMA placement decisions for memory allocation
    and migration, influencing where pages live but not how they are managed.


================================================================================
END
================================================================================
