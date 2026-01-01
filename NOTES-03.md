# 1. 
================================================================================
   GUEST USERSPACE READ — INCLUDING THE *PROCESS* PAGE TABLE (CR3)
   SHADOW MMU (NO EPT/NPT): WHOSE PT? WHICH CR3? WHO WALKS WHAT?
================================================================================

YOU SAID:
---------
"there will be a page table for the user process also in the guest"

YES.
The user process runs in a PER-PROCESS VIRTUAL ADDRESS SPACE in the guest.
That address space is defined by the guest's current CR3.

The crucial point:
  - Guest CR3 points to the GUEST PROCESS PAGE TABLE ROOT (GUEST PML4/PGD)
  - Hardware CR3 (while in guest) points to the SHADOW ROOT (SHADOW PML4)

================================================================================
1) THE 4 “TABLES” PEOPLE CONFUSE
================================================================================

(1) Guest Process Page Tables   (PER PROCESS, selected by guest CR3)
    - The page tables that define a particular user process address space
    - Owned by guest kernel
    - Root is pointed to by guest CR3
    - Maps:
         user region (lower addresses)
         kernel region (higher addresses / shared)

(2) Guest "Kernel Page Tables"
    - Usually NOT separate tables at runtime in classic x86-64 Linux:
        kernel mappings are typically present in every process pgd/pml4
        (shared upper half), but with supervisor permissions.
    - Conceptually still part of the SAME guest CR3 hierarchy.

(3) Shadow Page Tables (KVM-created)
    - Root loaded into the hardware CR3 used during guest execution
    - Hardware walks these
    - Maps GVA -> HPA

(4) Host Page Tables
    - Used when host kernel/userspace runs
    - Not used while guest is executing in non-root mode

================================================================================
2) THE TWO CR3 VALUES (THIS IS THE KEY)
================================================================================

-------------------------------
Guest CR3 (a guest register)
-------------------------------
vcpu->cr3 in KVM
Meaning:
    GPA of guest PML4/PGD root for the CURRENT PROCESS

This changes on process context switch inside guest.

--------------------------------------------
Hardware CR3 used while guest is running
--------------------------------------------
Set by KVM (vmcs/vmcb):
    vcpu->mmu.root_hpa   (shadow root)

Hardware uses THIS for page walks in guest mode.

================================================================================
3) WHAT "PROCESS PAGE TABLE" MEANS IN PRACTICE
================================================================================

A guest process has:

   guest CR3  -->  guest PML4 (process root)
                    |__ user mappings (U=1)
                    |__ kernel mappings (U=0, shared across processes)

So yes: "there is a page table for user process" == guest CR3 root.

KVM must respect that.
But hardware cannot walk it directly (shadow paging case).

================================================================================
4) FULL TRANSLATION FOR A USERSPACE READ
================================================================================

Instruction in guest userspace:

    MOV RAX, [GVA]

The translation must reflect the CURRENT guest process CR3.

So conceptually we want:

    (CURRENT PROCESS)
    GVA --(guest CR3 walk)--> GPA --(memslot)--> HPA

But in shadow paging, hardware does:

    GVA --(shadow CR3 walk)--> HPA

So KVM must build shadow PT entries that correspond to:
    guest CR3's mappings.

================================================================================
5) STEP-BY-STEP WORKFLOW (CORRECTED)
================================================================================

----------------------------------------------------------------
STEP 0: Guest is running a specific process
----------------------------------------------------------------

Guest scheduler selected process P
Guest loaded CR3 = P->pgd physical (guest physical)

So:
    vcpu->cr3 = guest_process_root_gpa

----------------------------------------------------------------
STEP 1: Guest executes userspace load
----------------------------------------------------------------

    MOV RAX, [GVA]

CPU begins translation.

----------------------------------------------------------------
STEP 2: Hardware walks SHADOW page tables
----------------------------------------------------------------

Hardware uses shadow CR3 (vcpu->mmu.root_hpa)
NOT guest CR3.

Case A: Shadow mapping exists
    -> read HPA -> done

Case B: Missing/permission mismatch
    -> #PF -> VMEXIT -> KVM handles

----------------------------------------------------------------
STEP 3: KVM software-walks the GUEST PROCESS page tables
----------------------------------------------------------------

KVM uses vcpu->cr3 (guest CR3) to walk guest PTs.

This is where the "process page table" is used.

KVM reads guest PT entries from guest memory:
    guest PML4E / PDPTE / PDE / PTE
Checks guest semantics:
    - present?
    - user access allowed? (U/S bits)
    - writable? (R/W)
    - NX for fetch?
    - etc.

Outcomes:
    (1) guest mapping invalid -> inject guest #PF -> return
    (2) guest mapping valid   -> produce GPA

----------------------------------------------------------------
STEP 4: KVM converts GPA -> HPA
----------------------------------------------------------------

Using memslots:
    gfn_to_memslot()
    gfn_to_page()
    page_to_pfn()

If missing slot:
    - MMIO or invalid -> special shadow marking

----------------------------------------------------------------
STEP 5: KVM installs/repairs shadow mapping
----------------------------------------------------------------

KVM ensures shadow PT has:
    GVA -> HPA mapping

This may involve creating intermediate shadow PT pages.

Leaf shadow PTE created with:
    - permissions derived from guest
    - plus KVM policy (dirty tracking, write traps)

Also:
    - rmap_add() for writable mappings
    - slot_bitmap updates for invalidation

----------------------------------------------------------------
STEP 6: Resume guest, re-execute instruction
----------------------------------------------------------------

Now shadow translation succeeds in hardware.
Read happens.
Guest continues.

================================================================================
6) HOW MANY PAGE TABLE SETS ARE THERE REALLY?
================================================================================

At runtime there are:

IN GUEST MEMORY:
  - One page table hierarchy per guest process (selected by guest CR3)
    (many processes => many pgds/pml4s in guest physical memory)

IN HOST/KVM MEMORY:
  - One shadow hierarchy per (guest CR3 context + role/mode details)
    cached via (gfn + role) and invalidated/zapped as needed

IN HOST KERNEL:
  - host page tables (separate, irrelevant while guest runs)

So yes:
  Guest has per-process PTs.
  KVM shadows them into shadow PTs.

================================================================================
7) THE ONE-LINE CORRECTION
================================================================================

My earlier text said:
  "Guest process page tables ..."

Your correction is:
  "Make explicit that these are PER-PROCESS, selected by guest CR3."

That is correct.

================================================================================
END
================================================================================



























































# 2.
================================================================================
                           KVM MEMSLOTS — DEEP EXPLANATION
                    (WHAT, WHY, HOW, AND HOW MMU USES THEM)
================================================================================

TL;DR (ONE SENTENCE)
-------------------
A memslot defines a CONTIGUOUS RANGE of GUEST PHYSICAL FRAMES
and tells KVM where the ACTUAL HOST PAGES for those frames live.

================================================================================
1. WHAT PROBLEM MEMSLOTS SOLVE
================================================================================

Guest thinks it has:

    GPA 0x00000000  --> RAM
    GPA 0x40000000  --> RAM
    GPA 0x80000000  --> RAM
    ...

Host reality:
    Guest RAM is NOT physically contiguous on the host.
    Guest RAM may be split, resized, removed, or backed by files.

KVM needs a structure that answers:

    "Given a GPA, where is the HOST MEMORY for it?"

This structure is a MEMSLOT.

================================================================================
2. WHAT A MEMSLOT REPRESENTS
================================================================================

struct kvm_memory_slot {
    gfn_t base_gfn;
    unsigned long npages;
    unsigned long flags;
    struct page **phys_mem;
    unsigned long *dirty_bitmap;
};

CONCEPTUAL VIEW
---------------

    Guest Physical Address Space (GPA)

    +------------------------------------------------------+
    |                                                      |
    |   [ base_gfn ................................ ]      |
    |   [           THIS MEMSLOT COVERS            ]      |
    |   [ base_gfn + npages - 1                   ]      |
    |                                                      |
    +------------------------------------------------------+

For ANY gfn in that range:

    host_page = phys_mem[gfn - base_gfn]

================================================================================
3. MEMBER-BY-MEMBER EXPLANATION
================================================================================

----------------------------------------------------------------
base_gfn
----------------------------------------------------------------
Guest Frame Number where this slot begins.

WHY:
----
Allows O(1) indexing:
    offset = gfn - base_gfn

----------------------------------------------------------------
npages
----------------------------------------------------------------
Number of guest pages in this slot.

WHY:
----
Defines the covered GPA range.

----------------------------------------------------------------
flags
----------------------------------------------------------------
Describes properties of this memory region.

Common meanings:
    - RAM vs IO
    - read-only regions
    - logging / special handling

WHY:
----
KVM must treat RAM and MMIO differently.

----------------------------------------------------------------
phys_mem
----------------------------------------------------------------
Array of host struct page pointers.

Indexing rule:
    phys_mem[i] == host page backing GFN (base_gfn + i)

WHY:
----
This is the CORE mapping:
    GPA -> struct page -> HPA

This is how KVM avoids page tables for GPA->HPA.

----------------------------------------------------------------
dirty_bitmap
----------------------------------------------------------------
One bit per guest page.

Set when:
    - guest writes to page
    - KVM marks page dirty

WHY:
----
Needed for:
    - live migration
    - snapshotting
    - incremental checkpointing

================================================================================
4. HOW MANY MEMSLOTS EXIST?
================================================================================

In your code:

    #define KVM_MEMORY_SLOTS 4

So:

    kvm->memslots[0..3]

Each slot can represent:
    - one RAM region
    - one MMIO hole
    - one file-backed region

Modern KVM supports many more slots, but concept is identical.

================================================================================
5. MEMSLOTS IN THE ADDRESS TRANSLATION PIPELINE
================================================================================

FULL PIPELINE FOR USERSPACE READ
--------------------------------

    Guest Instruction:
        MOV RAX, [GVA]

    Hardware:
        GVA --(shadow PT)--> HPA
              |
              | miss
              v

    KVM Software Walk:
        GVA --(guest PT)--> GPA

    MEMSLOT STEP:
        GPA -> GFN
        GFN -> memslot
        memslot -> struct page
        struct page -> HPA

This step is PURELY TABLE LOOKUP.

================================================================================
6. HOW KVM FINDS THE MEMSLOT
================================================================================

Code conceptually does:

    slot = gfn_to_memslot(kvm, gfn)

Which means:

    for each slot in kvm->memslots:
        if gfn in [base_gfn, base_gfn + npages):
            return slot

WHY LINEAR SEARCH IS OK HERE:
-----------------------------
- Very small number of slots
- Hot path optimized elsewhere
- Simple correctness beats complexity

================================================================================
7. MEMSLOTS VS PAGE TABLES (CRITICAL DIFFERENCE)
================================================================================

PAGE TABLES:
    - VA -> PA translation
    - hierarchical
    - permission-based

MEMSLOTS:
    - GPA -> host memory mapping
    - flat ranges
    - no permissions (mostly)

Memslots are NOT page tables.
They are MEMORY OWNERSHIP DESCRIPTORS.

================================================================================
8. MEMSLOTS AND MMIO
================================================================================

If GPA is NOT covered by any memslot:

    gfn_to_memslot() returns NULL

This means:
    - MMIO access
    - or invalid memory

KVM responds by:
    - marking shadow PTE as IO
    - clearing PRESENT
    - future accesses VMEXIT immediately

This is how device emulation is triggered.

================================================================================
9. MEMSLOTS AND RMAP
================================================================================

RMAP lives in:

    struct page (host page backing guest RAM)

That struct page comes FROM:

    memslot->phys_mem[]

So the chain is:

    GFN
      -> memslot
         -> phys_mem[]
            -> struct page
               -> page->private (rmap)

WITHOUT memslots:
    rmap has no anchor.

================================================================================
10. MEMSLOTS AND DIRTY TRACKING
================================================================================

When KVM allows a writable shadow PTE:

    mark_page_dirty(kvm, gfn)

Which does:
    set bit in slot->dirty_bitmap[gfn - base_gfn]

WHY THIS DESIGN:
----------------
- Dirty tracking is per memory region
- Migration tools operate on memslots
- Slot boundaries matter for invalidation

================================================================================
11. MEMSLOTS AND SHADOW PAGE INVALIDATION
================================================================================

Each shadow PT page tracks:

    slot_bitmap (one bit per memslot)

Meaning:
    "This shadow PT page contains mappings from these slots"

When a slot changes:
    - KVM scans only shadow pages with that slot bit set
    - Clears write permissions or zaps pages

WITHOUT slot_bitmap:
    full shadow PT scan required (too slow)

================================================================================
12. MEMSLOTS AND HOTPLUG / RESIZE
================================================================================

When guest memory is:
    - added
    - removed
    - resized

KVM:
    - updates memslots
    - bumps memory_config_version
    - invalidates affected shadow mappings
    - forces rebuild on next access

This is why memslots are FIRST-CLASS objects.

================================================================================
13. MENTAL MODEL (LOCK THIS IN)
================================================================================

Guest Physical Address Space
        |
        v
     MEMSLOTS
        |
        v
   struct page (host)
        |
        v
      HPA

Page tables never participate in GPA -> HPA.

================================================================================
14. ONE-SCREEN SUMMARY
================================================================================

Memslots are:
    - guest RAM layout
    - GPA ownership map
    - backing storage descriptor

They are used for:
    - GPA -> HPA translation
    - MMIO detection
    - dirty tracking
    - shadow invalidation
    - migration

They are NOT:
    - page tables
    - permission checks
    - address translation logic

================================================================================
END OF FILE
================================================================================









































































================================================================================
   HOW A GUEST USERSPACE LOAD WORKS (SHADOW MMU, PRE-EPT)
   From GVA to HPA — ALL PAGE TABLES, ALL STEPS
================================================================================

SCENARIO
--------
- Guest OS is Linux
- Guest runs a userspace process
- Instruction:   MOV RAX, [0x7f1234567890]
- Shadow paging (NO EPT / NPT)
- Paging enabled, long mode (4-level)

================================================================================
1. THE THREE ADDRESS SPACES (ABSOLUTELY CRITICAL)
================================================================================

GVA (Guest Virtual Address)
    - What the guest CPU instruction uses
    - Per-process address space

GPA (Guest Physical Address)
    - What the guest OS thinks is physical memory

HPA (Host Physical Address)
    - Real machine memory

The CPU can ONLY translate:
    VA -> PA (using CR3)

So KVM must ensure:
    Guest VA -> Host PA

================================================================================
2. HOW MANY PAGE TABLE HIERARCHIES EXIST?
================================================================================

ANSWER: **THREE CONCEPTUAL HIERARCHIES**

----------------------------------------------------------------
(1) Guest Process Page Tables
----------------------------------------------------------------
Owner: Guest kernel
Used by: KVM (software walk)
Purpose: GVA -> GPA

Levels (x86-64):
    Guest PML4
      -> Guest PDPT
         -> Guest PD
            -> Guest PT
               -> GPA

----------------------------------------------------------------
(2) Shadow Page Tables
----------------------------------------------------------------
Owner: KVM
Used by: HARDWARE
Purpose: GVA -> HPA

Levels (x86-64):
    Shadow PML4
      -> Shadow PDPT
         -> Shadow PD
            -> Shadow PT
               -> HPA

----------------------------------------------------------------
(3) Host Page Tables
----------------------------------------------------------------
Owner: Host kernel
Used by: CPU when NOT in guest
Purpose: HVA -> HPA

NOT used while guest runs.

================================================================================
3. IMPORTANT RULE (THIS EXPLAINS EVERYTHING)
================================================================================

While the guest is RUNNING:

    Hardware walks ONLY SHADOW PAGE TABLES

    Hardware NEVER walks guest page tables

Guest page tables are DATA, not control structures.

================================================================================
4. INITIAL STATE (BEFORE ACCESS)
================================================================================

- Guest process has valid guest page tables
- Shadow page tables are EMPTY or PARTIAL
- Shadow CR3 points to shadow root (kvm_mmu_page)
- No mapping yet for this GVA

================================================================================
5. STEP-BY-STEP: GUEST USERSPACE READ
================================================================================

----------------------------------------------------------------
STEP 1: Guest executes instruction
----------------------------------------------------------------

Guest CPU (in VMX non-root mode):

    MOV RAX, [0x7f1234567890]

CPU treats this EXACTLY like native execution.

----------------------------------------------------------------
STEP 2: Hardware page walk (SHADOW)
----------------------------------------------------------------

CPU:
    - Uses CR3 (shadow root)
    - Walks shadow PML4 / PDPT / PD / PT

Case A: Mapping exists
----------------------
    - Translation succeeds
    - Data read from HPA
    - Guest continues
    - KVM NOT INVOLVED

Case B: Mapping missing or violates permission
----------------------------------------------
    - Page fault (#PF)
    - VMEXIT to host
    - KVM gets control

We focus on Case B (interesting case).

================================================================================
6. VMEXIT: SHADOW PAGE FAULT
================================================================================

VMEXIT reason: Page Fault

CPU provides:
    - faulting linear address (CR2)
    - error code (read/write/user/present/etc)

KVM entry point:

    kvm_mmu_page_fault(vcpu, gva, error_code)

================================================================================
7. SOFTWARE WALK OF GUEST PAGE TABLES
================================================================================

IMPORTANT:
----------
This walk is done by KVM CODE, not hardware.

----------------------------------------------------------------
STEP 3: GVA -> GPA (guest page tables)
----------------------------------------------------------------

KVM calls:

    vcpu->mmu.gva_to_gpa(vcpu, gva)

This does:

    - Read guest CR3
    - Walk guest PML4
    - Walk guest PDPT
    - Walk guest PD
    - Walk guest PT

Checks:
    - present bit
    - user/supervisor
    - write permissions
    - NX (if instruction fetch)

OUTCOMES:

(1) Guest mapping invalid
------------------------
    - KVM injects #PF into guest
    - Guest kernel handles it
    - DONE

(2) Guest mapping valid
----------------------
    - GPA obtained
    - Continue

================================================================================
8. GPA -> HPA (MEMSLOT TRANSLATION)
================================================================================

----------------------------------------------------------------
STEP 4: GPA -> HPA
----------------------------------------------------------------

KVM does:

    slot = gfn_to_memslot(kvm, gpa >> PAGE_SHIFT)
    page = slot->phys_mem[gfn - base_gfn]
    hpa  = page_to_pfn(page) << PAGE_SHIFT

If no slot:
    - MMIO or invalid memory
    - Mark shadow entry as IO
    - Future access causes VMEXIT

================================================================================
9. SHADOW PAGE TABLE CONSTRUCTION
================================================================================

----------------------------------------------------------------
STEP 5: Build shadow mapping
----------------------------------------------------------------

KVM now ensures:

    GVA -> HPA exists in SHADOW PTs

For EACH level (top-down):

    - Lookup shadow page (kvm_mmu_page)
    - If missing:
          allocate shadow PT page
          link parent -> child
    - Continue to next level

At leaf level:

    - Create shadow PTE:
          PRESENT
          USER
          maybe WRITABLE
          points to HPA

    - Add rmap entry:
          (guest data page -> this shadow PTE)

    - Mark page dirty if writable

IMPORTANT:
----------
Shadow PT entries store:
    HPA (not GPA)

================================================================================
10. RESUME GUEST
================================================================================

----------------------------------------------------------------
STEP 6: Return to guest
----------------------------------------------------------------

KVM returns from VMEXIT.

CPU restarts instruction.

Hardware walks SHADOW PT again.

This time:
    - Translation succeeds
    - Data read from HPA
    - Instruction completes

From guest point of view:
    "Memory just worked."

================================================================================
11. WHAT PAGE TABLES WERE USED (SUMMARY)
================================================================================

+-----------------------------+-----------------------------+
| Stage                       | Page tables used            |
+-----------------------------+-----------------------------+
| Instruction execution       | Shadow page tables          |
| Hardware page walk          | Shadow page tables          |
| Fault resolution            | Guest page tables (software)|
| Final memory access         | Shadow page tables          |
+-----------------------------+-----------------------------+

Hardware NEVER walks guest page tables.

================================================================================
12. HOW MANY PAGE TABLE PAGES ARE TOUCHED?
================================================================================

Worst case (first access):

Guest side (software):
    - 4 guest PT pages (PML4, PDPT, PD, PT)

Shadow side:
    - Up to 4 shadow PT pages created
    - 1 leaf shadow PTE installed

Host side:
    - NO host page tables involved (guest running)

================================================================================
13. WHY THIS IS SLOW (AND WHY EPT EXISTS)
================================================================================

First access cost:
    - VMEXIT
    - Software PT walk
    - Shadow PT allocation
    - TLB flush

Subsequent access:
    - Fully hardware
    - Fast

EPT/NPT removes:
    - Software walk
    - Shadow PT maintenance
    - rmap complexity

================================================================================
14. FINAL MENTAL MODEL (MEMORIZE THIS)
================================================================================

Guest userspace load:

    CPU ---> SHADOW PAGE TABLES ---> HPA

Shadow PTs are:
    - Built lazily
    - Derived from guest PTs
    - Enforced by KVM

Guest PTs are:
    - Read by KVM
    - Never used by hardware

================================================================================
END OF FILE
================================================================================

