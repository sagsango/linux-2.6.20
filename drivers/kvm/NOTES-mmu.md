# BIG PICTURE: WHO TOUCHES WHAT
QEMU (userspace)
  │  ioctls (/dev/kvm)
  ▼
KVM core (kvm.c)
  │  installs memslots, creates VCPUs, calls mmu_setup/reset
  ▼
KVM MMU (this file)
  ├─ Mode-specific contexts: nonpaging / 32 / 32E-PAE / 64
  ├─ Shadow page tables (alloc, lookup, fill, zap)
  ├─ Reverse maps (rmap) for dirty/WP management
  ├─ Page-fault handlers (build shadow, map GPA→HPA)
  ├─ CR3/CR0/CR4 transitions → reset context + roots
  └─ Auditing (debug-only)
  ▼
Arch ops (vmx/svm)
  ├─ set_cr3/tlb_flush/inject_page_fault/...
  └─ vm-entry/vm-exit machinery


# CORE TYPE HIERARCHY (MMU + SHADOW PAGES + RMAP)
struct kvm_vcpu
  ├─ struct kvm_mmu mmu
  │   ├─ root_hpa               (64-bit root page or PAE root page array)
  │   ├─ pae_root[4]            (DMA32 page for PAE roots)
  │   ├─ root_level             (guest: 0/2/3/4)
  │   ├─ shadow_root_level      (shadow: 2/3/4)
  │   ├─ function ptrs:
  │   │   new_cr3, page_fault, gva_to_gpa, free
  │   └─ caches: mmu_pte_chain_cache / mmu_rmap_desc_cache
  │
  ├─ lists for shadow page memory:
  │   ├─ kvm->active_mmu_pages  (shared across vcpus in VM)
  │   └─ vcpu->free_pages       (per-vcpu pool of zeroed shadow pages)
  │
  └─ bookkeeping for fast path:
      ├─ last_pt_write_gfn, last_pt_write_count
      ├─ pdptrs[4] (PAE)
      └─ regs/crX/efer/rflags... (in arch layer)



# SHADOW PAGE OBJECT & HASH
struct kvm_mmu_page (one per shadow page)
  ├─ page_hpa       : HPA of the shadow page (host page frame)
  ├─ gfn            : guest frame number this page shadows
  ├─ role.word      : packed bits (level, root glevels, quadrant, metaphysical)
  ├─ slot_bitmap    : which memslots the page maps (for slot WP pass)
  ├─ parent_ptrs    : parent PTEs that reference this page (single/multi)
  ├─ root_count     : referenced as MMU root?
  ├─ global         : whether global TLB page (cleared on user mapping)
  ├─ hash_link      : in kvm->mmu_page_hash[...]
  └─ link           : in active_mmu_pages or vcpu->free_pages

HASH TABLE (per-VM)
  kvm->mmu_page_hash[ KVM_NUM_MMU_PAGES ]  → hlist of kvm_mmu_page
  key: gfn (+ role.word)  // role includes level/quadrant/metaphysical


# REVERSE MAP (RMAP) FOR DIRTY/WP
Goal: quickly find all shadow PTEs that point to a given *host* page.

On host struct page:
  page->private encodes:
    0             → no mappings
    pointer & ~1  → one mapping: (u64 *shadow_pte)
    (desc | 1)    → many mappings: kvm_rmap_desc chain

kvm_rmap_desc:
  ├─ shadow_ptes[RMAP_EXT]  // array of u64* pointing to shadow PTEs
  └─ *more                  // linked list for overflow


rmap_add(spte):
  if !present/writable PTE → ignore
  else:
    page = pfn_to_page(spte->PFN)
    if private == 0        → private = spte
    else if single         → move single into desc[0], put spte into desc[1], private = (desc|1)
    else (desc chain)      → append spte to first empty slot; allocate .more on overflow

rmap_remove(spte):
  inverse of add (collapse/remodel desc or clear private)


# ACCESS & BIT FIELDS (SELECTED)
PTE bits (subset used in shadow):
  PRESENT=bit0  WRITABLE=bit1  USER=bit2  ACCESSED=bit5  DIRTY=bit6
  PAGE_SIZE=bit7  GLOBAL=bit8  NX=bit63
KVM extra shadow flags piggybacked in available bits:
  PT_SHADOW_IO_MARK         (marks MMIO/non-mappable GPA)
  PT_SHADOW_WRITABLE_MASK   (desired writeability before WP logic)
  PT_SHADOW_USER_MASK       (mirror of user-bit decisions)


# TRANSLATION LADDERS
GVA ──(guest PTs in guest RAM)──▶ GPA ──(memslot lookup)──▶ HPA
             ▲                                    │
             │ (shadow tracks this)               └─ gfn_to_page(slot,gfn)

# ENTRY POINTS: CONTEXT LIFECYCLE
kvm_mmu_create(vcpu)
  ├─ alloc_mmu_pages()
  │   ├─ allocate KVM_NUM_MMU_PAGES host pages → vcpu->free_pages pool
  │   └─ allocate DMA32 page → vcpu->mmu.pae_root[4] = INVALID_PAGE
  └─ (no roots yet)

kvm_mmu_setup(vcpu)
  └─ init_kvm_mmu(vcpu) → picks mode:
       ├─ !paging            → nonpaging_init_context()
       ├─ long mode          → paging64_init_context()
       ├─ PAE                → paging32E_init_context()
       └─ plain 32-bit PT    → paging32_init_context()
     each:
       ├─ set function pointers (new_cr3, page_fault, gva_to_gpa, free)
       ├─ set root_level & shadow_root_level
       ├─ mmu_alloc_roots() → get root shadow page(s)
       └─ arch_ops->set_cr3(vcpu, mmu.root_hpa or pae_root HPA)



# ROOTS & QUADRANTS
64-bit guest (PT64_ROOT_LEVEL=4)
  vcpu->mmu.root_hpa = shadow PML4 page HPA
  (single 4-level root)

32E-PAE guest (PT32E_ROOT_LEVEL=3)
  vcpu->mmu.pae_root[4] = shadow PDPT entries (each PRESENT|HPA)
  vcpu->mmu.root_hpa = __pa(pae_root)   // hardware sees this as CR3

32-bit guest (PT32_ROOT_LEVEL=2)
  shadow_root_level is PT32E (3) to use PAE shadow internally
  (quadrant logic used to split 32-bit to 64-bit template)


# MMU RESET ON MODE/CR CHANGES
kvm_mmu_reset_context(vcpu)
  ├─ destroy_kvm_mmu(vcpu) → mmu.free() + invalidate root_hpa
  ├─ init_kvm_mmu(vcpu)    → re-pick mode, alloc roots, set CR3
  └─ mmu_topup_memory_caches(vcpu)


# PAGE FAULTS (SLOW PATH BUILDS SHADOW PTs)
nonpaging_page_fault(vcpu, gva, ec):
  ├─ top-up caches
  ├─ paddr = gpa_to_hpa(vcpu, gva)          // GPA == GVA
  ├─ if error: return 1 → let user side/MMIO handle
  └─ nonpaging_map(vcpu, align_down(gva), paddr)
        ▼
nonpaging_map():
  level = PT32E_ROOT_LEVEL (shadow 3-level)
  table = root_hpa
  loop level ↓ to 1:
    idx = PT64_INDEX(v, level)
    if level>1 and table[idx]==0:
       child = kvm_mmu_get_page(vcpu, pseudo_gfn(v,level-1), v, level-1,
                                metaphysical=1, &table[idx])
       table[idx] = child->page_hpa | PRESENT|WRITABLE|USER
    else if level==1:
       table[idx] = paddr | PRESENT|WRITABLE|USER
       rmap_add(&table[idx]); mark_page_dirty(GFN(v))


# PAGING GUEST (32/PAE/64): HIGH-LEVEL
paging*_page_fault(vcpu, gva, err):
  ├─ walk guest PTs (from guest memory) to get GPA + access bits
  ├─ resolve GPA→HPA via memslots
  ├─ allocate/link shadow tables for each level along the path
  ├─ set leaf shadow PTE with HPA and access_bits (after WP checks)
  └─ return; guest resumes (TLB now has GVA→HPA via shadow)

# SHADOW PAGE CREATION / LOOKUP
kvm_mmu_get_page(vcpu, gfn, gaddr, level, metaphysical, parent_pte*)
  role.word = (glevels=mmu.root_level, level, quadrant(if 32-bit), metaphysical)
  hash = gfn % KVM_NUM_MMU_PAGES
  if page exists (matching role) → mmu_page_add_parent_pte(page,parent)
  else:
    page = kvm_mmu_alloc_page()         // from vcpu->free_pages
    page->gfn = gfn; page->role = role; parent_pte pointer recorded
    hlist_add_head(hash_bucket, page)
    if !metaphysical: rmap_write_protect(vcpu, gfn)  // invalidate writable leaves

# SETTING A LEAF SHADOW PTE (CRITICAL PATH)
set_pte_common(vcpu, shadow_pte*, gaddr(GPA), dirty, access_bits, gfn):
  1) mirror access bits into KVM-shadow bits region
  2) if !dirty → clear WRITABLE in access_bits (COW/WP discipline)
  3) paddr = gpa_to_hpa(vcpu, gaddr)
  4) if HPA error:
       *shadow_pte |= gaddr; set IO_MARK; clear PRESENT; return
  5) *shadow_pte |= paddr | access_bits
     if page not GLOBAL in this chain → mark_pagetable_nonglobal()
  6) if WRITABLE:
       if a shadow page also exists for this gfn (upper levels present),
         clear WRITABLE (to force WP on page-table pages)
         if PTE was writable → clear and tlb_flush()
  7) if WRITABLE → mark_page_dirty(kvm, gfn)
  8) page_header_update_slot(kvm, shadow_pte, gaddr)  // for slot-wide WP
  9) rmap_add(vcpu, shadow_pte)

# WRITE PROTECTION & DIRTY LOGGING
rmap_write_protect(vcpu, gfn):
  for each spte referencing host page backing gfn:
    rmap_remove(spte)
    tlb_flush(vcpu)
    clear PT_WRITABLE_MASK on *spte

kvm_mmu_slot_remove_write_access(vcpu, slot_id):
  for each active_mmu_page with slot_bitmap having bit(slot_id):
    for each PTE in that page:
      if WRITABLE:
        rmap_remove(spte)
        clear WRITABLE

mark_page_dirty(kvm, gfn):
  find memslot containing gfn and set dirty bit (used by GET_DIRTY_LOG)


# HOST WRITE TO GUEST PT PAGES (COHERENCE)
kvm_mmu_pre_write(vcpu, gpa, bytes):
  gfn = gpa>>12
  scan hash bucket for kvm_mmu_page with page->gfn==gfn and !metaphysical
  for each:
    compute spte covering the byte-range (reject if misaligned or flooded)
    if present:
      if leaf → rmap_remove(spte)
      else    → mmu_page_remove_parent_pte(child, spte)
    *spte = 0
  (no post_write work needed here)

# ZAP & FREE LIFECYCLE
kvm_mmu_zap_page(vcpu, page):
  while page has parent_pte(s):
    kvm_mmu_put_page(vcpu, page, parent)
    *parent_pte = 0
  kvm_mmu_page_unlink_children(vcpu, page)  // clear child links & rmaps
  if !root_count:
    del from hash; move to vcpu->free_pages; n_free_mmu_pages++
  else:
    keep in active_mmu_pages (root pinned)

kvm_mmu_free_some_pages(vcpu):
  while n_free_mmu_pages < KVM_REFILL_PAGES:
    page = tail(active_mmu_pages)
    kvm_mmu_zap_page(vcpu, page)

# CR3 SWITCH HANDLING
paging_new_cr3(vcpu):
  mmu_free_roots(vcpu)
  if low free pool → kvm_mmu_free_some_pages()
  mmu_alloc_roots(vcpu)
  kvm_mmu_flush_tlb(vcpu)      // stat + arch tlb_flush
  arch_ops->set_cr3(vcpu, mmu.root_hpa or pae_root HPA | CR3 PC bits)

# ROOT ALLOCATION DETAILS
mmu_alloc_roots(vcpu):
  root_gfn = cr3>>12

  if shadow_root_level == 4 (64-bit):
    page = kvm_mmu_get_page(vcpu, root_gfn, 0, level=4, metaphysical=0, parent=NULL)
    ++page->root_count
    mmu.root_hpa = page->page_hpa
    return

  // PAE or shadow-PAE for 32-bit guests:
  for i in 0..3:
    if guest root_level == 3 (PAE):
       root_gfn = pdptrs[i] >> 12
    else if root_level == 0 (nonpaging):
       root_gfn = 0
    page = kvm_mmu_get_page(vcpu, root_gfn, (i<<30), level=2, metaphysical=!is_paging(vcpu), parent=NULL)
    ++page->root_count
    mmu.pae_root[i] = page->page_hpa | PRESENT
  mmu.root_hpa = __pa(mmu.pae_root)


# TLB & GLOBAL HANDLING
kvm_mmu_flush_tlb(vcpu) → kvm_arch_ops->tlb_flush(vcpu)
mark_pagetable_nonglobal(shadow_pte) → clears 'global' in page_header
(future TLB flushes ensure kernel/user split correctness when needed)

# GVA→GPA HELPERS
gva_to_hpa(vcpu,gva):
  gpa = vcpu->mmu.gva_to_gpa(vcpu, gva)
  if gpa==UNMAPPED → return UNMAPPED
  return gpa_to_hpa(vcpu, gpa)

gpa_to_hpa(vcpu,gpa):
  slot = gfn_to_memslot(kvm, gfn=gpa>>12)
  if !slot → return HPA_ERR
  page = gfn_to_page(slot,gfn)
  return (page_to_pfn(page)<<12) | (gpa & (PAGE_SIZE-1))

# MEMORY CACHES (ALLOC FAST PATH)
mmu_topup_memory_caches(vcpu):
  ensure:
    mmu_pte_chain_cache has ≥4 objects (struct kvm_pte_chain)
    mmu_rmap_desc_cache  has ≥1 object (struct kvm_rmap_desc)

mmu_memory_cache_alloc/free():
  small object pools; spill to kfree when full

# AUDIT (DEBUG ONLY; OFF BY DEFAULT)
kvm_mmu_audit(vcpu,"msg"):
  audit_rmap()             // compare rmap count vs. actual writable PTEs
  audit_write_protection() // shadow page tables must not have writable leafs
  audit_mappings()         // walk shadow, compare leaf HPA with gpa_to_hpa(gva)


# ERROR/PRECAUTIONS
safe_gpa_to_hpa(vcpu,gpa):
  hpa = gpa_to_hpa()
  if error → return bad_page_address | offset   // points to zeroed "bad" page

is_empty_shadow_page(page_hpa):
  ensures we only recycle clean, zeroed shadow pages

ASSERT()s:
  sanity for empty pages, proper role/level, parent_pte unlinking, etc.

# FULL PAGE-FAULT FLOW (PAGING GUEST) — STEP MAP
(1) VMEXIT: #PF with (gva, error_code)
(2) vcpu->mmu.page_fault(vcpu, gva, ec)
    ├─ walk guest PT in guest RAM to find GPA + guest perms
    ├─ resolve GPA→HPA (memslot)
    ├─ descend shadow levels:
    │     for each level L from root→leaf:
    │       - lookup child shadow page by (gfn_at_L, role(L), quadrant)
    │       - if not found → allocate child, link parent PTE
    └─ at leaf:
          set_pte_common(vcpu, &shadow_leaf, GPA, dirty?, access_bits, leaf_gfn)
(3) return to host; vm-entry; guest retries → hits shadow TLB → runs

# FULL WRITE FLOW TO GUEST PT PAGE (HOST OR GUEST MODIFIES PTE MEMORY)
(guest stores to guest-PT page) or (QEMU writes via kvm_write_guest)
  ├─ kvm_mmu_pre_write(vcpu, gpa, len):
  │     - find any shadow pages that mirror this gfn
  │     - for each affected shadow PTE:
  │         if leaf: rmap_remove(spte)
  │         else   : unlink parent from child page (mmu_page_remove_parent_pte)
  │         spte = 0
  └─ guest continues; future gva access will fault and rebuild with new mapping


# ONE-LINERS CHEAT SHEET (WHAT-DOES-WHAT)
kvm_mmu_create/setup/destroy         : lifecycle
init_kvm_mmu / destroy_kvm_mmu       : choose context & install/uninstall roots
mmu_alloc_roots / mmu_free_roots     : manage root shadow pages
kvm_mmu_get_page / _lookup_page      : obtain shadow page by (gfn,role)
set_pte_common                       : install a leaf mapping (GPA→HPA) with WP/dirty/rmap
kvm_mmu_page_unlink_children         : disconnect all child refs and rmaps
kvm_mmu_zap_page                     : fully invalidate one shadow page
kvm_mmu_unprotect_page(_virt)        : force-write-protect by gfn (or from gva)
kvm_mmu_slot_remove_write_access     : slot-wide write-protect sweep (for dirty logging)
kvm_mmu_pre_write                    : before guest/HV writes to guest-PT memory
nonpaging_* / paging*_init_context   : set function ptrs per guest mode
paging_new_cr3                       : CR3 switch handling (free→alloc roots + TLB flush)


# grok
1. MMU Initialization Flow
┌───────────────────────────────────────┐
│          init_kvm_mmu(vcpu)           │
└────────────────────┬──────────────────┘
                     │
        ┌────────────┴─────────────┐
        │ is_paging(vcpu)?         │
        └──────┬───────┬───────────┘
               │       │
        NO     ▼       ▼ YES
        nonpaging_init_context()
               │
               ▼
        root_level = 0
        shadow_root_level = PT32E_ROOT_LEVEL
        mmu_alloc_roots() → create 4 PAE roots
               │
               ▼
        kvm_arch_ops->set_cr3(root_hpa)

2. MMU Context Reset (CR3 Change / Paging Mode Change)
┌───────────────────────────────────────┐
│       kvm_mmu_reset_context()         │
└────────────────────┬──────────────────┘
                     │
                     ▼
              destroy_kvm_mmu()
                     │
              ┌──────┴───────┐
              │ free roots    │ → mmu_free_roots()
              └──────┬───────┘
                     │
                     ▼
               init_kvm_mmu()
                     │
        ┌────────────┴─────────────┐
        │ determine paging mode     │ → nonpaging / 32 / 32E / 64
        └────────────┬─────────────┘
                     │
                     ▼
              mmu_alloc_roots()
                     │
              ┌──────┴───────┐
              │ set CR3      │ → kvm_arch_ops->set_cr3()
              └──────────────┘

3. Page Fault Handling (Main Entry)
guest page fault → kvm_arch_ops->handle_exit()
        │
        ▼
vcpu->mmu.page_fault(vcpu, gva, error_code)
        │
   ┌────┴─────┐
   │          │
nonpaging    paging64_page_fault()
   │          │
   ▼          ▼
gpa_to_hpa() → nonpaging_map() → walk/create shadow PT
                     │
                     ▼
              set_pte_common()
                     │
              ┌──────┴───────┐
              │ rmap_add()   │ → track writable SPTE
              └──────┬───────┘
                     │
                     ▼
              mark_page_dirty()

4. Shadow Page Table Walk (paging_tmpl.h - 64-bit)
┌─────────────────────────────────────────────┐
│           paging64_page_fault()             │
└────────────────────┬────────────────────────┘
                     │
                     ▼
              walk_addr(vcpu, gva)
                     │
        ┌────────────┴─────────────┐
        │ level = root_level (4/3/2) │
        └────────────┬─────────────┘
                     │
                     ▼
            for (level; level > 1; --level)
                     │
              ┌──────┴───────┐
              │ fetch PTE    │ → FNAME(gpte)(spte)
              └──────┬───────┘
                     │
               ┌─────┴──────┐
               │ PT_PRESENT?│
               └───┬────┬───┘
                   │    │
                 NO     ▼ YES
                   │    │
                   ▼    ▼
            kvm_mmu_get_page()
                   │
            ┌──────┴───────┐
            │ alloc shadow │ → from free_pages
            └──────┬───────┘
                   │
                   ▼
            link parent_pte
                   │
                   ▼
            *parent_pte = new_page_hpa | PRESENT|WRITABLE|USER
                   │
                   ▼
            continue walk

5. kvm_mmu_get_page() - Shadow Page Allocation
┌─────────────────────────────────────────────┐
│           kvm_mmu_get_page()                │
└────────────────────┬────────────────────────┘
                     │
                     ▼
         hash_lookup(gfn, role) → mmu_page_hash[]
                     │
               ┌─────┴──────┐
               │   found?   │
               └───┬────┬───┘
                   │    │
                 YES    ▼ NO
                   │    │
                   ▼    ▼
        add parent_pte     kvm_mmu_alloc_page()
                   │           │
                   ▼           ▼
          return page       alloc from vcpu->free_pages
                               │
                               ▼
                        init role, gfn, hash_link
                               │
                               ▼
                        if (!metaphysical) rmap_write_protect()
                               │
                               ▼
                        return new page

6. Reverse Mapping (rmap) - Add Writable SPTE
┌─────────────────────────────────────────────┐
│                rmap_add()                   │
└────────────────────┬────────────────────────┘
                     │
                     ▼
            page = pfn_to_page(spte >> PAGE_SHIFT)
                     │
               ┌─────┴──────┐
               │ page->private?│
               └───┬────┬───┘
                   │    │
                 NULL   ▼ YES
                   │    │
                   ▼    ▼______________
            page->private = spte       │
                   │                   │
                   ▼                   ▼
               0 → 1              private & 1?
                                       │
                                     YES   NO
                                       │     │
                                       ▼     ▼
                                 alloc rmap_desc   private = spte
                                       │
                                       ▼
                                 desc->shadow_ptes[0] = old_spte
                                 desc->shadow_ptes[1] = spte
                                 page->private = desc | 1


7. rmap_remove() - Remove Writable Mapping
┌─────────────────────────────────────────────┐
│               rmap_remove()                 │
└────────────────────┬────────────────────────┘
                     │
                     ▼
            page = pfn_to_page(spte)
                     │
               ┌─────┴──────┐
               │ page->private & 1?│
               └───┬────┬───┘
                   │    │
                 YES   NO
                   │    │
                   ▼    ▼
            traverse desc chain       page->private = spte
                   │                   │
                   ▼                   ▼
            find spte → shift entries  if last → page->private = 0
                   │
                   ▼
            free desc if empty


8. Write Protection on Memory Slot Change
┌─────────────────────────────────────────────┐
│     kvm_mmu_slot_remove_write_access()      │
└────────────────────┬────────────────────────┘
                     │
                     ▼
        list_for_each(page in active_mmu_pages)
                     │
               ┌─────┴──────┐
               │ slot in bitmap?│
               └───┬────┬───┘
                   │    │
                 NO     ▼ YES
                        │
                        ▼
                   for each pte in page
                        │
                  ┌─────┴──────┐
                  │ PT_WRITABLE?│
                  └───┬────┬───┘
                      │    │
                    NO     ▼ YES
                           │
                           ▼
                      rmap_remove()
                           │
                           ▼
                      pte &= ~WRITABLE
                           │
                           ▼
                      tlb_flush()

9. kvm_mmu_pre_write() - Guest Writes to Page Table
┌─────────────────────────────────────────────┐
│            kvm_mmu_pre_write()              │
└────────────────────┬────────────────────────┘
                     │
                     ▼
              gfn = gpa >> PAGE_SHIFT
                     │
                     ▼
          hash_lookup all shadow pages for gfn
                     │
               ┌─────┴──────┐
               │   found?   │
               └───┬────┬───┘
                   │    │
                 YES   NO
                   │    │
                   ▼    ▼
            if misaligned or flooded → zap page
                   │
                   ▼
            clear parent_pte in higher level
                   │
                   ▼
            if leaf → rmap_remove()


10. Root Allocation (mmu_alloc_roots)
┌─────────────────────────────────────────────┐
│             mmu_alloc_roots()               │
└────────────────────┬────────────────────────┘
                     │
        ┌────────────┴─────────────┐
        │ shadow_root_level == 4?   │
        └──────┬───────┬───────────┘
               │       │
              NO       ▼ YES
               │       │
               ▼       ▼
        for i=0..3       get_page(root_gfn, level=4)
               │           │
               ▼           ▼
        get_page(gfn, i<<30, level=2)   ++page->root_count
               │
               ▼
        pae_root[i] = page_hpa | PRESENT
               │
               ▼
        root_hpa = __pa(pae_root)


11. Page Zapping (kvm_mmu_zap_page)
┌─────────────────────────────────────────────┐
│             kvm_mmu_zap_page()              │
└────────────────────┬────────────────────────┘
                     │
                     ▼
        while (multimapped || parent_pte)
                     │
               ┌─────┴──────┐
               │ clear parent_pte│ → *parent_pte = 0
               └──────┬───────┘
                     │
                     ▼
        unlink_children() → rmap_remove all
                     │
                     ▼
        if (!root_count) → free_page + del hash
        else → move to active_mmu_pages


12. Memory Cache Management
┌─────────────────────────────────────────────┐
│         mmu_topup_memory_caches()           │
└────────────────────┬────────────────────────┘
                     │
                     ▼
        topup pte_chain_cache (sizeof(pte_chain))
                     │
                     ▼
        topup rmap_desc_cache (sizeof(rmap_desc))
                     │
                     ▼
        alloc → kzalloc(GFP_NOWAIT) → cache->objects[]


13. Audit Flow (CONFIG_AUDIT)
┌─────────────────────────────────────────────┐
│             kvm_mmu_audit()                 │
└────────────────────┬────────────────────────┘
                     │
                     ▼
              audit_rmap()
                     │
               count_rmaps() vs count_writable_mappings()
                     │
                     ▼
              audit_write_protection()
                     │
               shadow page gfn must NOT be writable
                     │
                     ▼
              audit_mappings()
                     │
               walk all levels → verify SPTE == gpa_to_hpa(gva)


14. Free MMU Pages on Destroy
┌─────────────────────────────────────────────┐
│             kvm_mmu_destroy()               │
└────────────────────┬────────────────────────┘
                     │
                     ▼
              destroy_kvm_mmu()
                     │
                     ▼
              free_mmu_pages()
                     │
         ┌───────┴────────┐
         │ zap all active  │ → kvm_mmu_zap_page()
         └───────┬────────┘
                 │
                 ▼
         free vcpu->free_pages → __free_page()
                 │
                 ▼
         free pae_root page
