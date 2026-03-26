================================================================================
FILE: linux_mm_memory_c_deep_flow.txt
TOPIC: memory.c — page tables, faults, COW, unmap, GUP
SOURCE: user-provided code :contentReference[oaicite:0]{index=0}
KERNEL ERA: Linux 2.6.x (4-level paging transition)
================================================================================


SECTION 0: WHAT THIS FILE REALLY IS
================================================================================

This is the **core of Linux virtual memory execution engine**.

If mm/mmap.c = "VMA management"
and mm/madvise.c = "policy hints"

then:

    memory.c = "actual execution of memory operations"

It handles:

    - page faults (major/minor)
    - copy-on-write (COW)
    - page table allocation
    - page table teardown
    - mapping / unmapping
    - get_user_pages (GUP)
    - remap_pfn_range (device mapping)


================================================================================
SECTION 1: BIG PICTURE (MENTAL MODEL)
================================================================================

Everything revolves around:

    VMA (policy + region)
        +
    Page tables (actual mapping)
        +
    struct page (physical memory metadata)

Core operations:

    FAULT PATH:
        VA → PTE missing → allocate / load

    UNMAP PATH:
        VA → remove PTE → free page

    FORK PATH:
        duplicate page tables (COW)

    DRIVER PATH:
        map physical pages → user space


================================================================================
SECTION 2: PAGE TABLE FREEING (TEARDOWN PATH)
================================================================================

Key functions:

    free_pgd_range()
        → free_pud_range()
            → free_pmd_range()
                → free_pte_range()

Hierarchy:

    PGD → PUD → PMD → PTE


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

free_pgd_range()
    |
    |-- iterate PGD entries
    |
    |-- free_pud_range()
            |
            |-- free_pmd_range()
                    |
                    |-- free_pte_range()
                            |
                            |-- free actual PTE page
                            |-- update accounting


IMPORTANT:

    This does NOT free actual data pages
    only page table structures

Actual pages are freed earlier via:

    zap_page_range()


================================================================================
SECTION 3: PAGE TABLE ALLOCATION
================================================================================

Key function:

    __pte_alloc()

Flow:

    allocate new PTE page
    lock mm->page_table_lock
    check if already populated
    install via pmd_populate()


--------------------------------------------------------------------------------
RACE HANDLING:
--------------------------------------------------------------------------------

Two threads may allocate simultaneously:

    thread A allocates
    thread B allocates

Only one wins:

    if (pmd_present)
        free new
    else
        install

Classic lock + double-check pattern



================================================================================
SECTION 4: vm_normal_page()
================================================================================

This is VERY IMPORTANT.

Purpose:

    Convert PTE → struct page*

BUT:

    not all mappings have struct page!


--------------------------------------------------------------------------------
CASE:
--------------------------------------------------------------------------------

VM_PFNMAP (device mapping):

    PTE → raw PFN
    no struct page

So:

    return NULL


--------------------------------------------------------------------------------
WHY IMPORTANT:
--------------------------------------------------------------------------------

This is the boundary between:

    "normal memory"
        vs
    "device / special mappings"


================================================================================
SECTION 5: FORK PATH (copy_page_range)
================================================================================

When fork() happens:

    parent → child address space copy


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

copy_page_range()
    |
    |-- walk PGD → PUD → PMD → PTE
    |
    |-- copy_one_pte()


--------------------------------------------------------------------------------
copy_one_pte():
--------------------------------------------------------------------------------

Cases:

1) PTE not present (swap/file)
    → duplicate swap entry

2) COW mapping
    → mark both parent + child read-only

3) shared mapping
    → mark clean

4) normal page:
    → increment refcount
    → update rmap


--------------------------------------------------------------------------------
KEY INSIGHT:
--------------------------------------------------------------------------------

fork() does NOT copy memory

it sets up:

    shared pages + write-protection

Actual copy happens later (COW fault)


================================================================================
SECTION 6: UNMAP PATH (zap_page_range)
================================================================================

Used by:

    munmap()
    madvise(DONTNEED)
    exit()

Flow:

    zap_page_range()
        |
        → unmap_vmas()
            |
            → unmap_page_range()
                |
                → zap_pte_range()


--------------------------------------------------------------------------------
zap_pte_range():
--------------------------------------------------------------------------------

Core logic:

    for each PTE:
        if present:
            remove mapping
            update RSS
            remove rmap
            free page

        if swap:
            free swap entry


--------------------------------------------------------------------------------
IMPORTANT:
--------------------------------------------------------------------------------

This is where:

    memory is actually freed


================================================================================
SECTION 7: get_user_pages (GUP)
================================================================================

One of the MOST important APIs.

Used by:

    - DMA
    - RDMA
    - KVM
    - drivers


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

get_user_pages()
    |
    |-- find VMA
    |
    |-- for each page:
            |
            |-- follow_page()
                    |
                    |-- walk page tables
                    |-- return struct page
            |
            |-- if missing:
                    → __handle_mm_fault()


--------------------------------------------------------------------------------
KEY IDEA:
--------------------------------------------------------------------------------

GUP = "pin user memory"

It ensures:

    page is present
    refcount increased


================================================================================
SECTION 8: PAGE FAULT FLOW
================================================================================

Triggered from:

    do_page_fault() → __handle_mm_fault()


Inside GUP:

    follow_page() fails
        →
    __handle_mm_fault()


--------------------------------------------------------------------------------
RESULT TYPES:
--------------------------------------------------------------------------------

VM_FAULT_MINOR
VM_FAULT_MAJOR
VM_FAULT_OOM
VM_FAULT_SIGBUS


--------------------------------------------------------------------------------
IMPORTANT:
--------------------------------------------------------------------------------

Fault path is NOT fully in this file
but:

    do_wp_page()
    do_swap_page()

are here


================================================================================
SECTION 9: COPY-ON-WRITE (do_wp_page)
================================================================================

THIS IS CRITICAL.

Triggered when:

    write to shared read-only page


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

do_wp_page()
    |
    |-- get old_page
    |
    |-- if reusable:
            just make writable

    |-- else:
            allocate new_page
            copy data
            replace PTE
            update rmap + RSS


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

Before:

    PTE → pageA (refcount=2, RO)

After write:

    parent → pageA (RO)
    child  → pageB (RW, copy)


--------------------------------------------------------------------------------
IMPORTANT:
--------------------------------------------------------------------------------

This is the heart of:

    fork() efficiency


================================================================================
SECTION 10: ZERO PAGE MAPPING
================================================================================

Function:

    zeromap_page_range()

Maps:

    ZERO_PAGE

Used for:

    demand-zero memory


--------------------------------------------------------------------------------
BENEFIT:
--------------------------------------------------------------------------------

no allocation until write

→ lazy allocation


================================================================================
SECTION 11: remap_pfn_range (DRIVER PATH)
================================================================================

Used by drivers:

    map physical memory → user space


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

remap_pfn_range()
    |
    |-- mark VMA:
            VM_IO
            VM_RESERVED
            VM_PFNMAP
    |
    |-- install PTEs with PFN


--------------------------------------------------------------------------------
IMPORTANT:
--------------------------------------------------------------------------------

No struct page
No normal memory semantics

This is:

    device memory mapping


================================================================================
SECTION 12: unmap_mapping_range (FILE TRUNCATION)
================================================================================

Used when:

    file is truncated


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

vmtruncate()
    |
    → unmap_mapping_range()
            |
            → walk all VMAs mapping file
            → zap pages


--------------------------------------------------------------------------------
KEY IDEA:
--------------------------------------------------------------------------------

File changes → invalidate user mappings


================================================================================
SECTION 13: SWAP READAHEAD
================================================================================

swapin_readahead()

Purpose:

    prefetch nearby swap pages

Optimization:

    sequential swap access


================================================================================
SECTION 14: KEY CONNECTIONS (VERY IMPORTANT)
================================================================================

--------------------------------------------------------------------------------
mmap.c
--------------------------------------------------------------------------------
    creates VMA
    memory.c uses it

--------------------------------------------------------------------------------
madvise.c
--------------------------------------------------------------------------------
    calls zap_page_range()
    (actual work done here)

--------------------------------------------------------------------------------
rmap.c
--------------------------------------------------------------------------------
    reverse mapping
    used in:
        page_remove_rmap()

--------------------------------------------------------------------------------
swap.c
--------------------------------------------------------------------------------
    swap entry management

--------------------------------------------------------------------------------
TLB subsystem
--------------------------------------------------------------------------------
    tlb_gather_mmu()
    tlb_finish_mmu()


================================================================================
SECTION 15: END-TO-END FLOWS
================================================================================

--------------------------------------------------------------------------------
1) PAGE FAULT (READ)
--------------------------------------------------------------------------------

user access →
    page fault →
        __handle_mm_fault →
            allocate page →
            install PTE


--------------------------------------------------------------------------------
2) PAGE FAULT (WRITE / COW)
--------------------------------------------------------------------------------

write to shared →
    do_wp_page →
        allocate new page →
        copy →
        update PTE


--------------------------------------------------------------------------------
3) FORK
--------------------------------------------------------------------------------

fork →
    copy_page_range →
        mark RO →
        share pages


--------------------------------------------------------------------------------
4) UNMAP
--------------------------------------------------------------------------------

munmap →
    zap_page_range →
        remove PTE →
        free page


--------------------------------------------------------------------------------
5) DRIVER MAP
--------------------------------------------------------------------------------

driver →
    remap_pfn_range →
        map device memory


================================================================================
SECTION 16: FINAL INTUITION
================================================================================

memory.c is:

    the "execution engine" of virtual memory

It:

    walks page tables
    modifies PTEs
    allocates/frees pages
    enforces policies from VMA


================================================================================
SECTION 17: ONE-LINE SUMMARY
================================================================================

    memory.c implements the actual mechanics of virtual memory:
    page faults, COW, mapping/unmapping, and page table manipulation.


================================================================================
END OF FILE
================================================================================
