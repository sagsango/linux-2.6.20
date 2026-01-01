================================================================================
        KVM SHADOW MMU — STRUCTURES, MEMBERS, WORKFLOW, WHY & HOW
                 (Linux 2.6.x, pre-EPT / pre-NPT)
================================================================================

THIS FILE IS:
-------------
- A deep explanation of mmu.c USING STRUCTURES
- A guide for READING the CODE YOU ALREADY HAVE
- A mental model, not a reimplementation

================================================================================
0. CORE IDEA (KEEP THIS IN MIND)
================================================================================

Hardware can only do:

    CR3 --> page tables --> HPA

Guest expects:

    GVA --> guest page tables --> GPA

KVM provides:

    SHADOW PAGE TABLES:
        GVA --> HPA

Everything in mmu.c exists to:
    - build shadow page tables lazily
    - keep them consistent with guest intent
    - revoke/adjust permissions efficiently

================================================================================
1. ADDRESS TYPES (WHY SO MANY?)
================================================================================

typedef unsigned long  gva_t;   // guest virtual address
typedef u64            gpa_t;   // guest physical address
typedef unsigned long  gfn_t;   // guest frame number (gpa >> PAGE_SHIFT)

typedef unsigned long  hva_t;   // host virtual address
typedef u64            hpa_t;   // host physical address
typedef unsigned long  hfn_t;   // host frame number

WHY:
----
- Shadow MMU must reason about ALL THREE address spaces:
    GVA (guest CPU view)
    GPA (guest memory view)
    HPA (host memory view)
- Explicit types prevent conceptual bugs while reading code

================================================================================
2. struct kvm_mmu_page_role
================================================================================

union kvm_mmu_page_role {
    unsigned word;
    struct {
        unsigned glevels : 4;
        unsigned level   : 4;
        unsigned quadrant: 2;
        unsigned pad     : 6;
        unsigned metaphysical : 1;
    };
};

WHAT THIS STRUCT IS:
-------------------
A COMPACT DESCRIPTION of what a shadow page table page represents.

WHY IT EXISTS:
--------------
Shadow page tables are cached and reused.
Two shadow pages mapping the SAME GFN but DIFFERENT CONTEXTS
must NOT be confused.

MEMBER EXPLANATION:
-------------------

glevels:
    - Total paging levels of the GUEST
    - 0  -> nonpaging / real mode
    - 2  -> 32-bit paging
    - 3  -> PAE
    - 4  -> 64-bit paging
    WHY:
      Guest paging mode changes the meaning of indices and hierarchy.

level:
    - Which level THIS shadow page corresponds to
    - 1 = leaf (PTE)
    - 2 = PDE
    - 3 = PDPTE
    - 4 = PML4
    WHY:
      Needed to know:
        - whether entries map data or other PTs
        - how to unlink children
        - how to compute indices

quadrant:
    - Used for 2-level guest paging on 64-bit hosts
    - Encodes which virtual quadrant this page maps
    WHY:
      Allows reuse of shadow pages even when address space is split.

metaphysical:
    - Indicates this page does NOT represent a real guest page
    - Used for:
        * nonpaging mode
        * huge-page emulation
    WHY:
      Some shadow PT pages exist only to glue the hierarchy together.
      They should not trigger rmap or write-protect logic.

================================================================================
3. struct kvm_mmu_page  (THE MOST IMPORTANT STRUCT)
================================================================================

struct kvm_mmu_page {
    struct list_head link;
    struct hlist_node hash_link;

    gfn_t gfn;
    union kvm_mmu_page_role role;

    hpa_t page_hpa;
    unsigned long slot_bitmap;

    int global;
    int multimapped;
    int root_count;

    union {
        u64 *parent_pte;
        struct hlist_head parent_ptes;
    };
};

WHAT THIS STRUCT REPRESENTS:
----------------------------
ONE SHADOW PAGE TABLE PAGE + ALL METADATA NEEDED TO MANAGE IT.

MEMBER-BY-MEMBER EXPLANATION:
-----------------------------

link:
    - Links this page into either:
        * free_pages list
        * active_mmu_pages list
    WHY:
      Shadow pages are a cache.
      KVM must recycle them under memory pressure.

hash_link:
    - Links into mmu_page_hash[]
    WHY:
      Fast lookup of existing shadow pages by (gfn, role).
      Avoids rebuilding identical shadow PT pages.

gfn:
    - Guest frame number this shadow page corresponds to
    WHY:
      Shadow pages are derived from guest pages.
      This is the primary lookup key.

role:
    - Describes HOW this page should be interpreted
    WHY:
      Same gfn can appear at multiple levels or modes.

page_hpa:
    - Host physical address of the ACTUAL shadow PT memory
    WHY:
      This is what hardware page walks use.

slot_bitmap:
    - One bit per memory slot
    - Bit is set if ANY entry in this shadow page maps memory from that slot
    WHY:
      When a memslot changes, KVM can find affected shadow pages quickly
      without scanning everything.

global:
    - Indicates all entries in this page are global
    WHY:
      Helps decide when TLB flushes are required.

multimapped:
    - Indicates whether more than one parent points to this page
    WHY:
      Determines whether parent_pte is single or a list.

root_count:
    - Number of active CR3 roots pointing to this page
    WHY:
      Root pages cannot be freed while active.

parent_pte (single):
    - Pointer to the shadow PTE that references this page
    WHY:
      Fast path for common case (single parent).

parent_ptes (list):
    - List of parent pointers (via kvm_pte_chain)
    WHY:
      Needed when shadow pages are shared by multiple parents.

================================================================================
4. struct kvm_pte_chain  (PARENT TRACKING FOR PT STRUCTURE)
================================================================================

struct kvm_pte_chain {
    u64 *parent_ptes[NR_PTE_CHAIN_ENTRIES];
    struct hlist_node link;
};

WHAT THIS IS:
-------------
A CHUNKED LIST of parent shadow PTE pointers.

WHY IT EXISTS:
--------------
Shadow page tables form a DAG, not a simple tree.
Pages can be shared.
When removing a shadow page, KVM must find ALL parents.

WHY CHUNKED ARRAY:
------------------
- Avoids allocating a new object per parent
- Small, cache-friendly
- Fast iteration during teardown

================================================================================
5. RMAP STRUCTURES (REVERSE MAP FOR DATA PAGES)
================================================================================

struct kvm_rmap_desc {
    u64 *shadow_ptes[RMAP_EXT];
    struct kvm_rmap_desc *more;
};

WHERE IT LIVES:
---------------
Stored indirectly via:

    host struct page -> page->private

WHAT IT REPRESENTS:
-------------------
For ONE guest DATA page:
    all SHADOW LEAF PTEs that map it

WHY RMAP EXISTS:
----------------
KVM must answer quickly:

    "This guest page changed —
     which shadow PTEs must be updated?"

Page tables are forward-only.
RMAP provides the missing backward edges.

WHY STORE IN struct page:
-------------------------
- struct page already represents physical memory
- O(1) lookup from GFN
- No global hash table needed
- Lifetime matches guest memory

================================================================================
6. struct kvm_mmu  (MMU PERSONALITY / POLICY)
================================================================================

struct kvm_mmu {
    void (*new_cr3)(struct kvm_vcpu *vcpu);
    int  (*page_fault)(struct kvm_vcpu *vcpu, gva_t gva, u32 err);
    void (*free)(struct kvm_vcpu *vcpu);
    gpa_t (*gva_to_gpa)(struct kvm_vcpu *vcpu, gva_t gva);

    hpa_t root_hpa;
    int root_level;
    int shadow_root_level;

    u64 *pae_root;
};

WHAT THIS STRUCT IS:
-------------------
An ABSTRACTION over different paging modes.

WHY IT EXISTS:
--------------
- Nonpaging
- 32-bit paging
- PAE
- 64-bit paging

All require different walking logic but the SAME framework.

MEMBER EXPLANATION:
-------------------

new_cr3:
    - Called when guest writes CR3
    WHY:
      Guest address space changed → shadow roots invalid.

page_fault:
    - Handles guest page faults
    WHY:
      Central entry for building shadow mappings.

free:
    - Frees shadow roots
    WHY:
      Used when MMU context is destroyed or reset.

gva_to_gpa:
    - Software walk of guest page tables
    WHY:
      Hardware walks shadow PTs, not guest PTs.

root_hpa:
    - Shadow root loaded into hardware CR3
    WHY:
      Entry point for hardware page walk.

root_level:
    - Guest paging depth
    WHY:
      Needed to interpret guest PT hierarchy.

shadow_root_level:
    - Shadow paging depth
    WHY:
      Shadow hierarchy may differ (e.g., PAE emulation).

pae_root:
    - Special root array for PAE/32-bit emulation
    WHY:
      Hardware CR3 width limitations.

================================================================================
7. struct kvm_vcpu (MMU-RELEVANT PARTS ONLY)
================================================================================

struct kvm_vcpu {
    struct kvm *kvm;

    unsigned long cr0, cr2, cr3, cr4;
    u64 pdptrs[4];
    u64 shadow_efer;

    struct list_head free_pages;
    struct kvm_mmu_page page_header_buf[KVM_NUM_MMU_PAGES];
    struct kvm_mmu mmu;

    struct kvm_mmu_memory_cache mmu_pte_chain_cache;
    struct kvm_mmu_memory_cache mmu_rmap_desc_cache;

    gfn_t last_pt_write_gfn;
    int   last_pt_write_count;
};

WHY THESE MEMBERS MATTER:
-------------------------

cr0/cr3/cr4/shadow_efer:
    - Define current guest paging mode
    - Control MMU personality selection

pdptrs:
    - Used in PAE paging
    - Needed to build correct shadow roots

free_pages:
    - Pool of unused shadow pages
    WHY:
      Avoid allocation during PF handling.

page_header_buf:
    - Static array of kvm_mmu_page headers
    WHY:
      Each shadow page must have metadata.

mmu:
    - Active MMU policy object

mmu_*_cache:
    - Preallocated objects for:
        * parent tracking
        * rmap expansion
    WHY:
      Page faults must not fail allocations.

last_pt_write_*:
    - Flood detection for guest PT writes
    WHY:
      Excessive writes usually mean:
        * page no longer a PT
        * fork/exec storm

================================================================================
8. HOW ALL STRUCTURES WORK TOGETHER (SUMMARY FLOW)
================================================================================

Guest access
  -> hardware walks shadow PTs
    -> miss / violation
      -> VMEXIT
        -> mmu.page_fault()
          -> gva_to_gpa()
          -> gpa_to_hpa()
          -> set_pte_common()
             -> update kvm_mmu_page
             -> add rmap entry
             -> update slot_bitmap

Guest PT write
  -> kvm_mmu_pre_write()
     -> find kvm_mmu_page by gfn
     -> invalidate entries
     -> rmap_remove / parent unlink

Memory pressure
  -> kvm_mmu_free_some_pages()
     -> kvm_mmu_zap_page()
        -> detach parents
        -> unlink children
        -> clear rmap
        -> return page to pool

================================================================================
9. FINAL MENTAL MODEL
================================================================================

kvm_mmu_page:
    "How shadow PT pages exist and are connected"

parent_pte / kvm_pte_chain:
    "How shadow PT pages are safely removed"

rmap:
    "How data page mappings are controlled"

struct kvm_mmu:
    "Which rules apply right now"

mmu.c is not just about paging —
it is about maintaining DERIVED STATE
in the presence of a mutable guest.

================================================================================
END OF FILE
================================================================================

















================================================================================
               KVM SHADOW MMU — DEEP SUMMARY, WORKFLOW, WHY & HOW
                    (Linux 2.6.x era, pre-EPT/NPT)
================================================================================

PURPOSE OF THIS FILE
-------------------
This is NOT code.
This is NOT a rewrite.

This is a COMPLETE MENTAL MODEL of:
  - why mmu.c exists
  - what problems it solves
  - how the data structures cooperate
  - when each mechanism is triggered
  - how to reason about correctness

Read this together with mmu.c and kvm.h.

================================================================================
0. THE FUNDAMENTAL PROBLEM KVM MUST SOLVE
================================================================================

The CPU can only do ONE thing in hardware:

    Walk page tables referenced by CR3:
        GVA --> (hardware walk) --> HPA

But a guest OS expects:

    GVA --> (guest page tables) --> GPA

KVM must therefore provide:

    A set of page tables that:
      - hardware can walk
      - represent guest intent
      - still give KVM control

These are called SHADOW PAGE TABLES.

================================================================================
1. THE THREE WORLDS OF MEMORY IN KVM
================================================================================

----------------------------------------------------------------
WORLD 1: GUEST PAGE TABLES (guest-owned)
----------------------------------------------------------------
- Live in guest physical memory
- Written by the guest OS
- Define GVA -> GPA
- KVM reads them, but never installs them into CR3

----------------------------------------------------------------
WORLD 2: SHADOW PAGE TABLES (KVM-owned)
----------------------------------------------------------------
- Live in host memory
- Installed into CR3 (via VMCS/VMCB)
- Define GVA -> HPA
- Hardware walks these directly

----------------------------------------------------------------
WORLD 3: REVERSE MAPPING (KVM bookkeeping)
----------------------------------------------------------------
- Not page tables
- Data structures that answer:
      "Which shadow mappings exist for this guest page?"

This third world is what makes shadow paging possible.

================================================================================
2. THE CORE DESIGN IDEA OF mmu.c
================================================================================

mmu.c is NOT a page-table walker only.
It is a CONSISTENCY ENGINE.

It enforces this invariant:

    Shadow mappings MUST reflect guest mappings
    AND KVM policy (dirty tracking, MMIO, protection).

To maintain this invariant, mmu.c must react to:

  1) Guest page faults
  2) Guest CR3 changes
  3) Guest page-table writes
  4) Memory-slot changes
  5) Memory pressure

================================================================================
3. THE KEY DATA STRUCTURES (WHAT THEY REPRESENT)
================================================================================

----------------------------------------------------------------
A. struct kvm_mmu_page  == "a shadow PT page object"
----------------------------------------------------------------

Represents ONE shadow page-table page (4KB of entries).

It combines:
  - the actual memory page (page_hpa)
  - identity (gfn + role)
  - topology (parent_pte(s))
  - lifecycle state (root_count, free/active list)
  - invalidation hints (slot_bitmap)

WHY IT EXISTS:
--------------
Raw page-table memory is not enough.
KVM needs:
  - reuse (cache shadow pages)
  - safe teardown (unlink parents/children)
  - fast invalidation
  - eviction under pressure

----------------------------------------------------------------
B. parent_pte / kvm_pte_chain  == "reverse links for PT structure"
----------------------------------------------------------------

Tracks:
    Which shadow PTEs point to THIS shadow PT page

Used when:
  - zapping shadow pages
  - tearing down roots
  - invalidating hierarchies

WHY IT EXISTS:
--------------
Page tables are a tree.
Trees need parent pointers if you want fast removal.

Without this, deleting a shadow PT page would require
scanning all shadow PTs to find references.

----------------------------------------------------------------
C. rmap (page->private + kvm_rmap_desc)
----------------------------------------------------------------

Tracks:
    Guest DATA page --> all SHADOW LEAF PTEs mapping it

Stored in:
    host struct page corresponding to guest page

WHY IT EXISTS:
--------------
KVM must be able to say:

  "This guest page changed / must be write-protected /
   must be unmapped — fix ALL shadow mappings."

Without rmap, this would require scanning EVERY shadow PT.

================================================================================
4. WHY RMAP AND parent_pte ARE DIFFERENT THINGS
================================================================================

They solve TWO OPPOSITE PROBLEMS.

----------------------------------------------------------------
parent_pte(s)
----------------------------------------------------------------
- Tracks shadow page-table topology
- Shadow PT page <-- parent SPTEs
- Used for tearing down PT STRUCTURE

----------------------------------------------------------------
rmap
----------------------------------------------------------------
- Tracks data page mappings
- Guest data page --> shadow LEAF SPTEs
- Used for write-protect, dirty tracking, MMIO, slot removal

Both are required.
Neither replaces the other.

================================================================================
5. HOW THE MMU IS ORGANIZED AT RUNTIME
================================================================================

Each VCPU has:

  - a pool of shadow pages (free_pages)
  - a set of active shadow pages (active_mmu_pages)
  - a hash table to find shadow pages by (gfn, role)
  - an MMU "personality" (struct kvm_mmu)

struct kvm_mmu contains function pointers that define:
  - how to handle page faults
  - how to translate GVA->GPA
  - how to react to CR3 writes
  - how to free roots

This allows the SAME engine to support:
  - nonpaging
  - 32-bit paging
  - PAE
  - 64-bit paging

================================================================================
6. LIFECYCLE OVERVIEW
================================================================================

----------------------------------------------------------------
STEP 0: CREATE
----------------------------------------------------------------
kvm_mmu_create()

- Allocate a pool of shadow PT pages
- Each page gets a kvm_mmu_page header
- Pages start on free_pages list

WHY:
----
Avoid dynamic allocation inside page-fault paths.

----------------------------------------------------------------
STEP 1: SETUP
----------------------------------------------------------------
kvm_mmu_setup()
  -> init_kvm_mmu()

- Decide paging mode based on CR0/CR4/EFER
- Fill struct kvm_mmu function pointers
- Allocate shadow root(s)
- Load shadow CR3 into hardware

----------------------------------------------------------------
STEP 2: LAZY POPULATION
----------------------------------------------------------------
Shadow page tables are EMPTY initially.

They are populated ONLY when:
  - guest faults
  - or guest touches memory

This keeps memory usage bounded.

================================================================================
7. THE PAGE FAULT WORKFLOW (MOST IMPORTANT PATH)
================================================================================

Guest executes memory access
 |
 v
Hardware walks SHADOW PT
 |
 |-- miss / violation -->
 |
VMEXIT: Page Fault
 |
 v
kvm_mmu_page_fault()
 |
 v
(top up caches, free pages if low)
 |
 v
vcpu->mmu.page_fault()

-----------------------------------------
A. GVA -> GPA (software walk)
-----------------------------------------
- Read guest page tables
- Enforce guest permissions
- If guest mapping invalid:
      inject guest page fault
      DONE

-----------------------------------------
B. GPA -> HPA (memslot lookup)
-----------------------------------------
- Translate via memslots
- If no slot:
      mark as MMIO / error

-----------------------------------------
C. Install/adjust shadow PTE
-----------------------------------------
- Apply guest permissions
- Apply KVM policy (dirty, write-trap)
- Add rmap entry
- Update slot_bitmap

-----------------------------------------
D. Resume guest
-----------------------------------------

KEY IDEA:
---------
Shadow PTs are DERIVED DATA.
They are rebuilt lazily and discarded aggressively.

================================================================================
8. WRITE PROTECTION & DIRTY TRACKING
================================================================================

KVM often wants:
  - first write to trap
  - mark page dirty
  - then allow writes

How this works:

1) Shadow PTE installed WITHOUT writable bit
2) Guest write causes PF
3) KVM marks page dirty
4) Shadow PTE updated to writable
5) rmap tracks all writable mappings

Later, if KVM needs to revoke write:
  - rmap allows clearing WR in ALL mappings instantly

================================================================================
9. GUEST PAGE TABLE WRITES (SUBTLE BUT CRITICAL)
================================================================================

Guest modifies its own page tables.

This can invalidate shadow mappings.

Entry:
  kvm_mmu_pre_write(gpa, bytes)

Logic:
  - Find shadow pages whose gfn == written page
  - Detect if write is:
        aligned and sane  -> surgical invalidation
        misaligned/flood  -> zap entire shadow page

Actions:
  - Remove affected shadow entries
  - Remove rmap entries or parent links
  - Force rebuild on next fault

WHY FLOOD DETECTION:
-------------------
Excessive writes usually mean:
  - page is no longer a PT
  - or fork/exec storm

Precision is dropped in favor of correctness.

================================================================================
10. ZAPPING SHADOW PAGES (GARBAGE COLLECTION)
================================================================================

kvm_mmu_zap_page()

This is the CLEANUP ENGINE.

It does, in order:

1) Detach from ALL parents
2) Unlink ALL children
3) Remove rmap entries (for leaf pages)
4) Remove from hash
5) Return to free pool

WHY THIS IS SAFE:
-----------------
Because:
  - parent_pte tracks topology
  - rmap tracks data mappings
  - root_count prevents removing active roots

================================================================================
11. MEMORY PRESSURE HANDLING
================================================================================

Shadow pages are limited.

When free pool gets low:
  kvm_mmu_free_some_pages()

Strategy:
  - Evict older shadow pages
  - Zapping is cheap because pages are derived
  - They will be rebuilt if needed

This is effectively an MMU-specific cache eviction.

================================================================================
12. MEMORY SLOT INVALIDATION
================================================================================

When a memslot changes:

KVM must:
  - remove write access
  - ensure no stale HPA mappings exist

slot_bitmap in kvm_mmu_page allows:
  - finding shadow pages that contain mappings
  - clearing writable bits via rmap

================================================================================
13. WHY THIS DESIGN IS HARD (AND HISTORICAL)
================================================================================

Shadow paging is difficult because:

- Hardware page tables are forward-only
- Guest page tables are mutable
- KVM must virtualize permissions
- Dirty tracking must be precise
- TLB behavior must be respected

mmu.c solves this with:
  - derived data (shadow PTs)
  - aggressive invalidation
  - reverse mappings
  - conservative correctness-first decisions

================================================================================
14. WHY EPT/NPT MADE THIS MOSTLY GO AWAY
================================================================================

With EPT/NPT:
  - Hardware does GVA -> GPA -> HPA
  - Guest page tables are real
  - Dirty bits are in hardware
  - No shadow PTs required

Therefore:
  - rmap is mostly unnecessary
  - parent_pte chains disappear
  - mmu.c becomes much simpler

This file represents the "hard mode" of virtualization.

================================================================================
15. HOW TO READ mmu.c AFTER THIS
================================================================================

When reading mmu.c, always ask:

1) Is this about:
     - shadow PT structure?  -> kvm_mmu_page / parent_pte
     - data mappings?        -> rmap
     - mode policy?          -> struct kvm_mmu

2) What event triggered this?
     - page fault
     - CR3 write
     - PT write
     - slot change
     - eviction

3) Is this code:
     - building derived state?
     - invalidating derived state?

If you keep that frame, mmu.c becomes logical instead of scary.

================================================================================
END OF FILE
================================================================================

