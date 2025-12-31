# driver/kvm/mmu.c
================================================================================
                     KVM SHADOW MMU (drivers/kvm/mmu.c)
                   Linux 2.6.x era — Pre-EPT / Pre-NPT
================================================================================

LEGEND
------
GVA = Guest Virtual Address
GPA = Guest Physical Address
HPA = Host Physical Address
GFN = Guest Frame Number
SPTE = Shadow Page Table Entry
RMAP = Reverse Mapping
PT   = Page Table

================================================================================
1. BIG PICTURE
================================================================================

               Guest CPU (VMX non-root)
                       |
                       | memory access (load/store/fetch)
                       v
               +----------------------+
               |  Shadow Page Tables  |   (owned by KVM)
               +----------------------+
                       |
                       | hardware page walk
                       v
               +----------------------+
               |   Host Physical RAM  |
               +----------------------+

IMPORTANT:
----------
- Guest page tables are NEVER installed into CR3
- Hardware walks SHADOW page tables
- KVM builds/maintains shadow PTs
- Guest PT writes are trapped & emulated
- Reverse mapping is REQUIRED for correctness

================================================================================
2. CORE DATA STRUCTURES
================================================================================

----------------------------------------------------------------
struct kvm_mmu_page  (ONE shadow page table page)
----------------------------------------------------------------

+------------------------------------------------------------+
| gfn            | Guest frame this page represents          |
| page_hpa       | Host physical addr of shadow PT page      |
| role           | {level, root, metaphysical, quadrant}     |
| root_count     | # of CR3 roots pointing here              |
| parent_pte     | single parent SPTE (fast path)            |
| parent_ptes    | list of parents (if multimapped)          |
| multimapped    | 0/1                                       |
| slot_bitmap    | memory slots this page maps               |
| global         | global bit tracking                       |
| link           | active/free lists                         |
| hash_link      | hash bucket                               |
+------------------------------------------------------------+

----------------------------------------------------------------
struct kvm_rmap_desc  (Reverse mapping extension)
----------------------------------------------------------------

+----------------------------------+
| shadow_ptes[4]  | SPTE pointers  |
| more            | next chunk     |
+----------------------------------+

Stored indirectly via:
    struct page -> page->private

================================================================================
3. REVERSE MAPPING (RMAP)
================================================================================

WHY?
----
Given a *guest physical page*:
    -> Find ALL shadow PTEs mapping it
    -> Required for:
        * write-protect
        * invalidation
        * dirty tracking
        * slot removal

STORAGE STRATEGY
----------------

page->private == 0
    -> no shadow mappings

page->private == SPTE
    -> exactly ONE shadow PTE

page->private == (rmap_desc | 1)
    -> MANY shadow PTEs

----------------------------------------------------------------
rmap_add(vcpu, spte)
----------------------------------------------------------------

IF spte is writable & present:
    page = pfn_to_page(spte->HPA)

CASE 1: page->private == 0
    page->private = spte

CASE 2: page->private == spte
    allocate rmap_desc
    desc[0] = old_spte
    desc[1] = new_spte
    page->private = desc | 1

CASE 3: already desc
    append to desc chain

----------------------------------------------------------------
rmap_remove(vcpu, spte)
----------------------------------------------------------------

Find spte in page->private
Remove it
Shrink desc if needed
Free desc if empty

================================================================================
4. SHADOW PAGE LIFECYCLE
================================================================================

----------------------------------------------------------------
ALLOC
----------------------------------------------------------------

kvm_mmu_alloc_page()

free_pages list
    |
    v
+---------------------+
| zeroed page         |
| role assigned       |
| parent_pte linked   |
+---------------------+
    |
    v
active_mmu_pages list

----------------------------------------------------------------
LOOKUP / REUSE
----------------------------------------------------------------

kvm_mmu_lookup_page()
    hash[gfn] -> role match -> reuse

kvm_mmu_get_page()
    if found:
        add parent_pte
    else:
        alloc new
        rmap_write_protect(gfn)

----------------------------------------------------------------
FREE / ZAP
----------------------------------------------------------------

kvm_mmu_zap_page()

WHILE page has parents:
    parent_pte = one parent
    *parent_pte = 0
    remove parent relationship

unlink children:
    if leaf:
        rmap_remove all SPTEs
    else:
        unlink child pages

if root_count == 0:
    remove from hash
    put back to free_pages

================================================================================
5. ADDRESS TRANSLATION PATHS
================================================================================

----------------------------------------------------------------
GPA -> HPA
----------------------------------------------------------------

gpa_to_hpa()

gpa
 |
 v
gfn_to_memslot()
 |
 v
gfn_to_page()
 |
 v
page_to_pfn()
 |
 v
HPA

Failure:
    return HPA_ERR_MASK

----------------------------------------------------------------
GVA -> GPA
----------------------------------------------------------------

vcpu->mmu.gva_to_gpa()

Depends on mode:
    - nonpaging
    - paging32
    - paging32E
    - paging64

Generated from paging_tmpl.h

----------------------------------------------------------------
GVA -> HPA
----------------------------------------------------------------

gva_to_hpa()
    gpa = gva_to_gpa()
    hpa = gpa_to_hpa()

================================================================================
6. PAGE FAULT HANDLING
================================================================================

----------------------------------------------------------------
NON-PAGING MODE
----------------------------------------------------------------

Guest PF
 |
 v
VMEXIT
 |
 v
nonpaging_page_fault()

addr = gva
paddr = gpa_to_hpa(addr)

nonpaging_map():
    walk shadow PTs
    allocate missing tables
    install SPTE
    rmap_add()

----------------------------------------------------------------
PAGING MODE
----------------------------------------------------------------

paging*_page_fault()

Flow:
-----
Guest PF
 |
 v
VMEXIT
 |
 v
Walk GUEST page tables
 |
 v
Compute GPA
 |
 v
Check permissions (RWX, U/S, NX)
 |
 v
Install SPTE
 |
 v
Resume guest

================================================================================
7. WRITE PROTECTION & DIRTY TRACKING
================================================================================

----------------------------------------------------------------
WHY WRITE-PROTECT?
----------------------------------------------------------------

- Track dirty pages
- Detect guest PT writes
- Maintain shadow consistency

----------------------------------------------------------------
set_pte_common()
----------------------------------------------------------------

Inputs:
    shadow_pte
    gpa
    access_bits
    dirty
    gfn

Steps:
------
1. Compute HPA
2. Handle MMIO (IO mark)
3. Clear WR if conflict
4. mark_page_dirty()
5. rmap_add()
6. update slot bitmap

----------------------------------------------------------------
rmap_write_protect(gfn)
----------------------------------------------------------------

FOR each SPTE mapping gfn:
    rmap_remove(spte)
    clear WR bit
    TLB flush

================================================================================
8. CR3 HANDLING & TLB
================================================================================

----------------------------------------------------------------
CR3 WRITE
----------------------------------------------------------------

paging_new_cr3()

Steps:
------
1. free old roots
2. maybe free shadow pages
3. alloc new roots
4. flush TLB
5. load CR3 (shadow root)

----------------------------------------------------------------
ROOT TYPES
----------------------------------------------------------------

64-bit:
    single PML4 root

PAE:
    4 PDPT roots

Stored in:
    vcpu->mmu.root_hpa
    vcpu->mmu.pae_root[]

================================================================================
9. GUEST PAGE TABLE WRITES
================================================================================

THIS IS CRITICAL

----------------------------------------------------------------
kvm_mmu_pre_write()
----------------------------------------------------------------

Triggered when guest writes GPA

Inputs:
    gpa, bytes

Steps:
------
1. gfn = gpa >> PAGE_SHIFT
2. find shadow pages for gfn
3. detect misaligned write
4. detect flooding
5. IF bad:
       zap shadow page
   ELSE:
       remove affected SPTE
       unlink child

WHY FLOOD DETECTION?
-------------------
- fork()
- page no longer used as PT
- excessive overhead otherwise

----------------------------------------------------------------
kvm_mmu_post_write()
----------------------------------------------------------------

(empty in this version)

================================================================================
10. ALLOCATION & TEARDOWN
================================================================================

----------------------------------------------------------------
CREATE
----------------------------------------------------------------

kvm_mmu_create()
    alloc_mmu_pages()
        - shadow page pool
        - PAE root page (DMA32)

----------------------------------------------------------------
SETUP
----------------------------------------------------------------

kvm_mmu_setup()
    init_kvm_mmu()
        choose mode:
            nonpaging
            paging32
            paging32E
            paging64

----------------------------------------------------------------
DESTROY
----------------------------------------------------------------

kvm_mmu_destroy()
    destroy_kvm_mmu()
    free_mmu_pages()
    free caches

================================================================================
11. COMPLETE EXECUTION FLOW (ONE ACCESS)
================================================================================

Guest executes: MOV [gva], reg
 |
 v
Hardware walks SHADOW PT
 |
 |-- miss -->
 |
VMEXIT: Page Fault
 |
 v
paging*_page_fault()
 |
 v
Walk GUEST PT
 |
 v
Compute GPA
 |
 v
gpa_to_hpa()
 |
 v
Install SPTE
 |
 v
rmap_add()
 |
 v
Resume Guest

================================================================================
12. WHY THIS MMU IS HARD
================================================================================

- Two page table worlds (guest + shadow)
- Permission virtualization
- TLB coherence
- Multi-parent shadow pages
- Write-fault emulation
- Slot invalidation
- Fork/exec behavior

THIS is why EPT/NPT was revolutionary.

================================================================================
END OF FILE
================================================================================

