================================================================================
          FULL FLOW: WHAT HAPPENS WHEN THE GUEST WRITES CR3 (SHADOW MMU)
                 Linux 2.6.x KVM, pre-EPT/NPT, detailed path
================================================================================

GOAL
----
Guest executes:      MOV CR3, new_cr3_value
We want:             Guest address space changes (new process / new pgd)
But hardware is using shadow page tables.
So KVM must:         (1) learn guest CR3 changed
                     (2) throw away old shadow roots (and maybe old shadows)
                     (3) allocate a new shadow root for the new guest CR3
                     (4) load the new shadow root into hardware CR3
                     (5) flush TLB so CPU doesn't use stale translations

================================================================================
0) IMPORTANT ENTITIES (WHO HOLDS WHAT)
================================================================================

GUEST VISIBLE REGISTER:
  - guest CR3 value stored in vcpu->cr3  (this is GPA of guest pgd/pml4)

HARDWARE CR3 USED WHILE RUNNING GUEST:
  - shadow CR3 stored in VMCS/VMCB
  - KVM sets it from: vcpu->mmu.root_hpa (this is HPA of shadow root)

SHADOW ROOT TRACKING:
  - vcpu->mmu.root_hpa
  - root page has page->root_count++ and can't be freed while root_count>0

SHADOW PAGE CACHE:
  - kvm->mmu_page_hash[]  (maps gfn+role -> kvm_mmu_page)
  - kvm->active_mmu_pages list + vcpu->free_pages list

================================================================================
1) GUEST EXECUTES MOV CR3, X
================================================================================

Guest instruction stream:

   ... (guest kernel doing context switch)
   MOV CR3, new_cr3

CPU is in VMX non-root mode (guest running).

This instruction is PRIVILEGED / CONTROL-REGISTER access.
In virtualization, accesses to CR3 are intercepted (depending on controls).
So:

   MOV CR3 triggers VMEXIT  (exit reason: control register access)

================================================================================
2) VMEXIT: CONTROL REGISTER ACCESS (CR3 WRITE)
================================================================================

VMEXIT transfers control to host kernel KVM run loop:

   userspace qemu
       ioctl(KVM_RUN)
           -> kvm_arch_ops->run()
              -> vmx/svm enters guest
              -> VMEXIT occurs
              -> vmx/svm exit handler runs in kernel

The exit handler decodes:
   - which CR is accessed (CR3)
   - read or write
   - the value being written (from guest register)

The handler then calls the common path that updates vcpu state.

(Exact function names differ a bit by old versions, but the logical steps are
the same: "handle cr write" -> "set_cr3()" -> "mmu.new_cr3()".)

================================================================================
3) KVM UPDATES ITS GUEST STATE: vcpu->cr3
================================================================================

The CR3 write handler will:
   - validate the new CR3 value (alignment, reserved bits, etc.)
   - update the stored guest register:

        vcpu->cr3 = new_cr3_value

IMPORTANT:
----------
This is still the GUEST CR3 (GPA).
Hardware CR3 is NOT set to this.

Now KVM must rebuild shadow roots for this new guest CR3.

So it calls the MMU hook:

        vcpu->mmu.new_cr3(vcpu)

In paging modes this is:

        paging_new_cr3(vcpu)

In nonpaging mode new_cr3 is empty.

================================================================================
4) paging_new_cr3(): THE CORE SHADOW-ROOT SWITCH
================================================================================

From your mmu.c:

    static void paging_new_cr3(struct kvm_vcpu *vcpu)
    {
        mmu_free_roots(vcpu);
        if (unlikely(vcpu->kvm->n_free_mmu_pages < KVM_MIN_FREE_MMU_PAGES))
            kvm_mmu_free_some_pages(vcpu);
        mmu_alloc_roots(vcpu);
        kvm_mmu_flush_tlb(vcpu);
        kvm_arch_ops->set_cr3(vcpu, vcpu->mmu.root_hpa);
    }

We now explain each step in detail.

--------------------------------------------------------------------------------
4.1) mmu_free_roots(vcpu)
--------------------------------------------------------------------------------

What it does:
  - Detaches the CURRENT shadow root(s) from being "active roots"
  - Decrements root_count on the root pages
  - Clears vcpu->mmu.root_hpa (and/or pae_root entries) to INVALID_PAGE

Case A: shadow_root_level == 4 (x86_64 root)
   root = vcpu->mmu.root_hpa
   page = page_header(root)
   page->root_count--
   vcpu->mmu.root_hpa = INVALID_PAGE

Case B: PAE/32-bit root (pae_root[4])
   for each i:
       root = vcpu->mmu.pae_root[i]  (points to shadow page_hpa)
       page = page_header(root & PT64_BASE_ADDR_MASK)
       page->root_count--
       vcpu->mmu.pae_root[i] = INVALID_PAGE
   vcpu->mmu.root_hpa = INVALID_PAGE

WHY THIS MATTERS:
  root_count prevents root pages from being freed while the CPU might still
  have them loaded. Once root_count hits 0, zap/free logic is allowed.

NOTE:
  This does not necessarily delete all shadow mappings immediately.
  It only removes "root references" so pages become eligible for eviction.

--------------------------------------------------------------------------------
4.2) OPTIONAL: free some shadow pages if low
--------------------------------------------------------------------------------

If KVM's free pool is low:

    if (n_free_mmu_pages < KVM_MIN_FREE_MMU_PAGES)
         kvm_mmu_free_some_pages(vcpu)

kvm_mmu_free_some_pages() loops:
    while (n_free_mmu_pages < KVM_REFILL_PAGES)
        pick a victim from active_mmu_pages
        kvm_mmu_zap_page(vcpu, victim)

Why we do this RIGHT NOW:
  Switching CR3 tends to cause many new faults soon.
  Those faults need new shadow PT pages.
  If pool is low, we'd fail allocations in hot path.

So we reclaim BEFORE we start building the new root.

What zapping does (high level):
  - detach parents (parent_pte(s))
  - unlink children
  - remove rmap entries for leaf pages
  - remove from hash
  - return page to vcpu->free_pages
  - increment kvm->n_free_mmu_pages

--------------------------------------------------------------------------------
4.3) mmu_alloc_roots(vcpu)
--------------------------------------------------------------------------------

This creates/obtains a shadow root appropriate for current guest mode.

Key input:
    root_gfn = vcpu->cr3 >> PAGE_SHIFT

Meaning:
    guest CR3 points to a guest page table page (in guest physical memory).
    That page lives at that GFN.

Now KVM needs a SHADOW page table page that corresponds to that guest root.

Case: 64-bit root (shadow_root_level == 4)
------------------------------------------
    page = kvm_mmu_get_page(vcpu,
                            gfn=root_gfn,
                            gaddr=0,
                            level=4,
                            metaphysical=0,
                            parent_pte=NULL);

    root = page->page_hpa
    page->root_count++
    vcpu->mmu.root_hpa = root

Important detail:
  kvm_mmu_get_page() uses (gfn + role) to look up in mmu_page_hash.
  If it exists, it reuses it.
  If not, it allocates from free_pages and inserts into hash.

Also:
  If not metaphysical, it calls rmap_write_protect(vcpu, gfn)
  (more on that later).

Case: 32-bit/PAE style roots
----------------------------
KVM fills vcpu->mmu.pae_root[0..3] with pointers to shadow tables
and sets:

    vcpu->mmu.root_hpa = __pa(vcpu->mmu.pae_root)

This is a synthetic root structure needed due to format differences.

WHAT YOU MUST UNDERSTAND:
-------------------------
This step does NOT populate all mappings.
It only sets up the top-level.
Everything else is populated lazily by page faults.

--------------------------------------------------------------------------------
4.4) kvm_mmu_flush_tlb(vcpu)
--------------------------------------------------------------------------------

This does:
   kvm_arch_ops->tlb_flush(vcpu)

WHY:
----
TLB contains translations derived from OLD shadow root.
Even if we changed CR3, virtualization/hardware state can be tricky.
KVM flushes to ensure:
    no stale translations remain that point to old shadow PTs.

(Old KVM is conservative; EPT generations in modern KVM make this smarter.)

--------------------------------------------------------------------------------
4.5) kvm_arch_ops->set_cr3(vcpu, vcpu->mmu.root_hpa | flags)
--------------------------------------------------------------------------------

This writes shadow CR3 to VMCS/VMCB:

Hardware-visible guest CR3 (while running) becomes:
    shadow_root_hpa

Now on VM entry / resume, the CPU uses new shadow page tables.

================================================================================
5) WHAT HAPPENS NEXT WHEN GUEST RUNS?
================================================================================

After paging_new_cr3 finishes:
  - the exit handler returns
  - KVM_RUN resumes guest execution

Guest continues after MOV CR3.

Now the guest will touch memory in its new address space.
Because shadow PT is new/empty-ish, guest will likely fault often.

Those faults trigger:

    kvm_mmu_page_fault()
      -> software walk guest page tables using NEW vcpu->cr3
      -> gpa_to_hpa via memslots
      -> install shadow entries lazily
      -> resume

So CR3 switch causes a burst of page faults to rebuild shadow state.

================================================================================
6) WHY rmap_write_protect(vcpu, root_gfn) IS CALLED DURING kvm_mmu_get_page
================================================================================

In your code:

    if (!metaphysical)
        rmap_write_protect(vcpu, gfn);

This is subtle.

The gfn passed into kvm_mmu_get_page() is the guest page table page gfn,
NOT a data page (in this root allocation case).

KVM write-protects mappings to that guest PT page so that:
    if guest writes to its PT page later,
    KVM will trap and can invalidate affected shadow entries.

This is the "watch guest page tables" mechanism.

So:
  - guest PT pages are treated specially
  - KVM wants to intercept writes to them
  - write-protecting their shadow mappings is the enforcement tool

================================================================================
7) COMPLETE FLOW SUMMARY (ONE SCREEN)
================================================================================

Guest kernel context switch:
    MOV CR3, new_root
        |
        v
VMEXIT (CR3 write)
        |
        v
KVM CR3 handler:
    vcpu->cr3 = new_root
    vcpu->mmu.new_cr3(vcpu)
        |
        v
paging_new_cr3:
    mmu_free_roots()                 (root_count--, invalidate old root_hpa)
    maybe free_some_pages()          (zap pages if pool low)
    mmu_alloc_roots()                (lookup/alloc new shadow root for new gfn)
    tlb_flush()
    set_cr3(shadow_root_hpa)         (hardware CR3 now points to shadow root)
        |
        v
Resume guest:
    next accesses fault
        |
        v
kvm_mmu_page_fault builds shadow mappings lazily for NEW guest CR3

================================================================================
8) WHAT THIS MEANS CONCEPTUALLY
================================================================================

Guest CR3 write means:
  "I'm switching to a different guest page table universe."

Shadow paging response:
  "OK, I will switch to a different shadow universe, and rebuild it lazily."

Nothing is copied eagerly.
Everything is derived on demand.

================================================================================
END
================================================================================


























================================================================================
   HOW SHADOW PAGE TABLES ARE UPDATED WHEN
   A GUEST PROCESS CHANGES GVA -> GPA MAPPINGS
   (SHADOW MMU, PRE-EPT/NPT)
================================================================================

THIS FILE EXPLAINS
------------------
- What “guest changes mapping” actually means
- Why hardware does NOT automatically notice
- How KVM *forces* notification
- The exact mechanisms used:
      * write-protect
      * rmap
      * pre_write hooks
      * zapping
- What is lazy vs eager

================================================================================
0) WHAT DOES "GUEST CHANGES GVA -> GPA" MEAN?
================================================================================

A guest process NEVER changes mappings directly.

Instead, the guest kernel modifies its OWN page tables in memory.

Typical operations:
    - mmap()
    - munmap()
    - mprotect()
    - page fault handler installing PTEs
    - fork()/exec()
    - context switch + page table updates

All of these boil down to:

    Guest kernel WRITES to guest page table pages
    (PML4 / PDPT / PD / PT pages in guest RAM)

================================================================================
1) THE FUNDAMENTAL PROBLEM
================================================================================

Hardware walks:
    SHADOW page tables (GVA -> HPA)

Guest modifies:
    GUEST page tables (GVA -> GPA)

Hardware:
    DOES NOT LOOK AT GUEST PAGE TABLES

So if the guest changes its PTs:
    Shadow PTs become STALE

Therefore:
    KVM MUST notice guest PT writes
    and invalidate/update shadow PTs

================================================================================
2) HOW DOES KVM EVEN SEE GUEST PAGE TABLE WRITES?
================================================================================

KEY IDEA:
---------
KVM write-protects guest page table pages
in the SHADOW page tables.

That means:
    - Guest PT pages are mapped READ-ONLY in shadow PTs
    - Any guest attempt to modify its PTs causes a #PF
    - That #PF VMEXITS to KVM

This is the ONLY way KVM learns about mapping changes.

================================================================================
3) HOW GUEST PAGE TABLE PAGES ARE IDENTIFIED
================================================================================

When KVM shadows a guest page table page:

    kvm_mmu_get_page(vcpu, gfn, ...)

If that page represents a guest PT page (not metaphysical):

    rmap_write_protect(vcpu, gfn)

This ensures:
    ALL shadow PTEs mapping this GFN
    have WRITABLE bit CLEARED

So:
    Guest PT pages are always write-protected.

================================================================================
4) THE CRITICAL GUARANTEE
================================================================================

GUEST CANNOT MODIFY PAGE TABLES WITHOUT TRAPPING INTO KVM

This is enforced by:
    shadow write-protection
    + hardware page faults

================================================================================
5) STEP-BY-STEP: GUEST PROCESS CHANGES A MAPPING
================================================================================

We now walk the full flow.

----------------------------------------------------------------
STEP 1: Guest kernel decides to change a mapping
----------------------------------------------------------------

Example:
    mmap() creates a new mapping
    OR page fault handler installs a PTE

Guest kernel executes:
    WRITE to a guest PT entry

This is a normal memory store instruction.

----------------------------------------------------------------
STEP 2: Hardware checks shadow PT permissions
----------------------------------------------------------------

Shadow PT entry for that PT page:
    - PRESENT
    - NOT WRITABLE

So the write:
    -> causes a #PF (write-protection fault)
    -> VMEXIT to KVM

----------------------------------------------------------------
STEP 3: VMEXIT arrives at KVM
----------------------------------------------------------------

KVM sees:
    - faulting GPA
    - error_code = WRITE + PRESENT

This is NOT a guest-visible PF yet.

KVM recognizes:
    "This is a write to a guest page table page."

----------------------------------------------------------------
STEP 4: kvm_mmu_pre_write() is invoked
----------------------------------------------------------------

KVM calls:

    kvm_mmu_pre_write(vcpu, gpa, bytes)

Purpose:
    - Invalidate shadow mappings derived from this PT page
    - BEFORE allowing the guest write to proceed

================================================================================
6) WHAT kvm_mmu_pre_write() ACTUALLY DOES
================================================================================

Input:
    gpa  = guest physical address being written
    gfn  = gpa >> PAGE_SHIFT

Algorithm (simplified):

    For each shadow page S where S->gfn == gfn:
        If S represents a shadowed PT page:
            Invalidate affected shadow entries

Two cases:

----------------------------------------------------------------
CASE A: Precise invalidation (common case)
----------------------------------------------------------------

If:
    - write is aligned
    - write size matches PTE size
    - write frequency is low

Then:
    - Find which shadow entry corresponds to that guest PTE
    - Remove:
          * rmap entry (if leaf)
          * parent link (if non-leaf)
    - Clear that shadow entry

Result:
    Only the affected mapping is removed.

----------------------------------------------------------------
CASE B: Conservative invalidation (flood / misaligned)
----------------------------------------------------------------

If:
    - misaligned write
    - too many writes (fork/exec storm)
    - ambiguous modification

Then:
    - kvm_mmu_zap_page()
    - ENTIRE shadow page is destroyed

Result:
    More page faults later, but correctness preserved.

================================================================================
7) AFTER INVALIDATION — HOW DOES GUEST WRITE CONTINUE?
================================================================================

Once shadow invalidation is done:

    KVM temporarily allows the write
    (or emulates it)

Guest PT page is updated in guest memory.

Shadow PT is now:
    - missing the old mapping
    - ready to be rebuilt lazily

================================================================================
8) WHAT HAPPENS ON THE *NEXT* MEMORY ACCESS?
================================================================================

Later, guest executes instruction accessing the affected GVA.

Hardware:
    walks shadow PT
    -> mapping missing
    -> #PF
    -> VMEXIT

KVM now:
    - software-walks UPDATED guest PTs
    - sees new GVA -> GPA mapping
    - installs new shadow PTE
    - resumes guest

This is LAZY RECONSTRUCTION.

================================================================================
9) WHY KVM DOES NOT "UPDATE" SHADOW PTs IMMEDIATELY
================================================================================

Important design choice:
    KVM INVALIDATES, not eagerly updates.

Reasons:
    - Guest may never touch that address again
    - Avoids unnecessary work
    - Reduces overhead during fork/exec storms
    - Keeps logic simpler and safer

So:
    Guest PT write
        -> shadow invalidation
        -> future access rebuilds shadow entry

================================================================================
10) ROLE OF RMAP IN THIS PROCESS
================================================================================

RMAP is used when:
    - invalidating DATA page mappings

Example:
    Guest PT write removes mapping for a data page

Then:
    - shadow leaf PTE pointed to that data page
    - rmap allows KVM to find and remove it instantly

WITHOUT rmap:
    - KVM would have to scan all shadow PTs

================================================================================
11) ROLE OF parent_pte TRACKING
================================================================================

When invalidating non-leaf shadow pages:

    parent_pte(s) tell KVM:
        "which higher-level shadow entries point here"

This allows:
    - safe unlinking of shadow PT hierarchy
    - no dangling references

================================================================================
12) COMPLETE FLOW (ONE SCREEN)
================================================================================

Guest kernel writes to its page table:
    |
    v
Shadow PT write-protect triggers #PF:
    |
    v
VMEXIT to KVM:
    |
    v
kvm_mmu_pre_write():
    |
    +-- precise remove shadow entry
    |      OR
    +-- zap entire shadow page
    |
    v
Guest PT write completes:
    |
    v
Future access:
    |
    v
kvm_mmu_page_fault():
    |
    v
Shadow mapping rebuilt from UPDATED guest PTs

================================================================================
13) FINAL MENTAL MODEL (MEMORIZE THIS)
================================================================================

Guest PTs are DATA.
Shadow PTs are DERIVED DATA.

When guest PTs change:
    Shadow PTs are INVALIDATED, not updated.

Write-protection is the notification mechanism.
Page faults are the synchronization points.

================================================================================
END
================================================================================

