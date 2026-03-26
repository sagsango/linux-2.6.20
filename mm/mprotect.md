/******************************************************************************
 * FILE: linux-2.6.20/mm/mprotect.c
 *
 * TITLE:
 *   FULL IDE NOTES — DETAILED SUMMARY + BACKGROUND + FLOW + CONCEPTS
 *
 * SOURCE:
 *   Provided code (linux-2.6.20 mprotect.c)
 *   :contentReference[oaicite:0]{index=0}
 *
 ******************************************************************************/

/******************************************************************************
 * 0. DETAILED SUMMARY FIRST (VERY IMPORTANT)
 *
 * This file implements:
 *
 *      mprotect() system call
 *
 * which allows a process to change permissions of an existing mapping:
 *
 *      PROT_READ
 *      PROT_WRITE
 *      PROT_EXEC
 *
 *
 * CORE IDEA:
 *
 *      mmap()   -> creates VMA + page tables
 *      mprotect() -> modifies permissions of existing VMA + PTEs
 *
 *
 * WHAT THIS FILE DOES:
 *
 *   1. Validate arguments (alignment, flags, ranges)
 *   2. Locate VMAs covering the range
 *   3. Split VMAs if partial region is targeted
 *   4. Merge VMAs if possible (optimization)
 *   5. Update VMA metadata (vm_flags, vm_page_prot)
 *   6. Walk page tables and update PTE permissions
 *   7. Flush TLB
 *
 *
 * MOST IMPORTANT TAKEAWAY:
 *
 *      mprotect is NOT just metadata change.
 *      It MUST update actual page tables.
 *
 ******************************************************************************/

/******************************************************************************
 * 1. BACKGROUND — WHAT IS mprotect?
 *
 * User space:
 *
 *      mprotect(addr, len, PROT_READ | PROT_WRITE)
 *
 * Means:
 *
 *      "Change access permissions of this virtual range"
 *
 *
 * Kernel responsibility:
 *
 *      - Update VMA flags (logical view)
 *      - Update page table entries (hardware view)
 *
 *
 * WHY BOTH?
 *
 *      VMA → used by kernel logic (fault handling, checks)
 *      PTE → used by hardware (MMU enforcement)
 *
 ******************************************************************************/

/******************************************************************************
 * 2. HIGH LEVEL FLOW
 *
 * USER CALL
 *     |
 *     v
 * sys_mprotect()
 *     |
 *     v
 * Walk VMAs + validate
 *     |
 *     v
 * mprotect_fixup()
 *     |
 *     v
 *   ├── split_vma() (if partial)
 *   ├── vma_merge() (if possible)
 *   ├── update vm_flags
 *   └── change_protection()
 *             |
 *             v
 *         walk page tables
 *             |
 *             v
 *         update PTE bits
 *             |
 *             v
 *         flush TLB
 *
 ******************************************************************************/

/******************************************************************************
 * 3. PAGE TABLE WALKING PIPELINE (VERY IMPORTANT)
 *
 * change_protection()
 *      → change_pud_range()
 *           → change_pmd_range()
 *                → change_pte_range()
 *
 *
 * This mirrors x86 page table hierarchy:
 *
 *      PGD → PUD → PMD → PTE
 *
 ******************************************************************************/

/******************************************************************************
 * 4. FUNCTION: change_pte_range()
 ******************************************************************************/

static void change_pte_range(...)

/*
 * This is the most important low-level function.
 *
 * It iterates over PTEs and modifies permissions.
 *
 * FLOW:
 *
 *   for each PTE:
 *       if present:
 *           clear PTE
 *           modify protection
 *           optionally set write bit
 *           install new PTE
 *
 * KEY IDEA:
 *
 *   ptep_get_and_clear()
 *       removes old mapping safely
 *
 *   pte_modify()
 *       updates protection bits
 *
 *   set_pte_at()
 *       installs updated mapping
 *
 *
 * WHY CLEAR FIRST?
 *
 *   To avoid SMP race with hardware-updated bits (dirty/accessed)
 *
 *
 * DIRTY HANDLING:
 *
 *   if (dirty_accountable && pte_dirty(ptent))
 *       make it writable
 *
 *   → prevents unnecessary write faults
 *
 *
 * MIGRATION CASE:
 *
 *   Handles swap/migration entries safely
 *
 ******************************************************************************/

/******************************************************************************
 * 5. FUNCTION: change_pmd_range()
 *
 * Just iterates PMD level and delegates to PTE level.
 ******************************************************************************/

static inline void change_pmd_range(...)

/*
 * Flow:
 *
 *   for each PMD entry:
 *       if valid:
 *           call change_pte_range()
 *
 ******************************************************************************/

/******************************************************************************
 * 6. FUNCTION: change_pud_range()
 *
 * Same idea at higher level.
 ******************************************************************************/

static inline void change_pud_range(...)

/*
 * PGD → PUD → PMD → PTE
 *
 * This function is just hierarchical traversal.
 *
 ******************************************************************************/

/******************************************************************************
 * 7. FUNCTION: change_protection()
 ******************************************************************************/

static void change_protection(...)

/*
 * Top-level page table walker.
 *
 * Steps:
 *
 *   1. flush_cache_range()
 *   2. walk PGD → PUD → PMD → PTE
 *   3. update all PTEs
 *   4. flush_tlb_range()
 *
 *
 * WHY CACHE FLUSH?
 *
 *   Ensure consistency between cache and new permissions
 *
 *
 * WHY TLB FLUSH?
 *
 *   CPU may cache old permissions → must invalidate
 *
 ******************************************************************************/

/******************************************************************************
 * 8. FUNCTION: mprotect_fixup()
 ******************************************************************************/

static int mprotect_fixup(...)

/*
 * This is the HEART of VMA-level logic.
 *
 * Responsibilities:
 *
 *   1. Handle accounting (commit memory)
 *   2. Try VMA merge
 *   3. Split VMA if needed
 *   4. Update vm_flags + vm_page_prot
 *   5. Call change_protection()
 *
 *
 * --------------------------------------------------------------------------
 * 8.1 Write permission accounting
 * --------------------------------------------------------------------------
 *
 * If making private mapping writable:
 *
 *      need to reserve memory (COW case)
 *
 *      security_vm_enough_memory()
 *
 *
 * --------------------------------------------------------------------------
 * 8.2 Merge optimization
 * --------------------------------------------------------------------------
 *
 * Try:
 *
 *      vma_merge()
 *
 * If successful:
 *
 *      no need to split or create new VMA
 *
 *
 * --------------------------------------------------------------------------
 * 8.3 Splitting
 * --------------------------------------------------------------------------
 *
 * If modifying partial region:
 *
 *      split_vma(start)
 *      split_vma(end)
 *
 *
 * --------------------------------------------------------------------------
 * 8.4 Update VMA metadata
 * --------------------------------------------------------------------------
 *
 *      vma->vm_flags = newflags
 *
 *      vma->vm_page_prot = protection_map[...]
 *
 *
 * --------------------------------------------------------------------------
 * 8.5 Apply to page tables
 * --------------------------------------------------------------------------
 *
 *      change_protection(...)
 *
 ******************************************************************************/

/******************************************************************************
 * 9. FUNCTION: sys_mprotect()
 ******************************************************************************/

asmlinkage long sys_mprotect(...)

/*
 * Entry point from user space.
 *
 *
 * --------------------------------------------------------------------------
 * 9.1 Validation
 * --------------------------------------------------------------------------
 *
 *   - page alignment
 *   - len != 0
 *   - overflow checks
 *   - valid prot flags
 *
 *
 * --------------------------------------------------------------------------
 * 9.2 Personality handling
 * --------------------------------------------------------------------------
 *
 *   READ_IMPLIES_EXEC:
 *
 *      some binaries expect:
 *          PROT_READ → also PROT_EXEC
 *
 *
 * --------------------------------------------------------------------------
 * 9.3 Locking
 * --------------------------------------------------------------------------
 *
 *      down_write(&mm->mmap_sem)
 *
 *   Required because:
 *      we modify VMA structures
 *
 *
 * --------------------------------------------------------------------------
 * 9.4 Find starting VMA
 * --------------------------------------------------------------------------
 *
 *      find_vma_prev()
 *
 *
 * --------------------------------------------------------------------------
 * 9.5 Iterate across VMAs
 * --------------------------------------------------------------------------
 *
 *   for each VMA covering range:
 *
 *       newflags = combine:
 *           - requested prot
 *           - existing may flags
 *
 *
 * --------------------------------------------------------------------------
 * 9.6 Permission validation
 * --------------------------------------------------------------------------
 *
 * Ensure:
 *
 *      requested permissions ⊆ allowed permissions
 *
 *
 * --------------------------------------------------------------------------
 * 9.7 Apply changes
 * --------------------------------------------------------------------------
 *
 *      mprotect_fixup()
 *
 *
 * --------------------------------------------------------------------------
 * 9.8 Move to next VMA
 * --------------------------------------------------------------------------
 *
 * Continue until full range covered
 *
 ******************************************************************************/

/******************************************************************************
 * 10. KEY CONCEPTS
 *
 * --------------------------------------------------------------------------
 * 10.1 VMA vs PTE
 * --------------------------------------------------------------------------
 *
 * VMA:
 *      logical region (kernel structure)
 *
 * PTE:
 *      hardware mapping (MMU)
 *
 *
 * mprotect MUST update both.
 *
 *
 * --------------------------------------------------------------------------
 * 10.2 Lazy MMU mode
 * --------------------------------------------------------------------------
 *
 * arch_enter_lazy_mmu_mode()
 *
 * Allows batching of updates for performance.
 *
 *
 * --------------------------------------------------------------------------
 * 10.3 Dirty tracking
 * --------------------------------------------------------------------------
 *
 * Avoid unnecessary faults by preserving dirty/write state.
 *
 *
 * --------------------------------------------------------------------------
 * 10.4 Commit accounting
 * --------------------------------------------------------------------------
 *
 * Private writable mapping may consume memory → tracked.
 *
 *
 * --------------------------------------------------------------------------
 * 10.5 TLB coherence
 * --------------------------------------------------------------------------
 *
 * After modifying PTEs:
 *
 *      flush_tlb_range()
 *
 ******************************************************************************/

/******************************************************************************
 * 11. ASCII FLOW (VERY IMPORTANT)
 *
 * USER:
 *   mprotect(addr, len, prot)
 *
 * KERNEL:
 *
 *   sys_mprotect
 *       |
 *       +--> validate args
 *       |
 *       +--> find VMA(s)
 *       |
 *       +--> for each VMA:
 *               |
 *               +--> mprotect_fixup
 *                       |
 *                       +--> split_vma (if needed)
 *                       +--> vma_merge (if possible)
 *                       +--> update vm_flags
 *                       +--> change_protection
 *                               |
 *                               +--> PGD → PUD → PMD → PTE
 *                               +--> modify PTE bits
 *                               +--> flush TLB
 *
 ******************************************************************************/

/******************************************************************************
 * 12. INTERVIEW-LEVEL INSIGHT
 *
 * Q: Why is mprotect expensive?
 *
 * A:
 *   Because it:
 *      - walks page tables
 *      - modifies many PTEs
 *      - flushes TLB
 *
 *
 * Q: Why not just change VMA?
 *
 * A:
 *   Hardware enforces permissions via PTE, not VMA.
 *
 *
 * Q: Why split VMAs?
 *
 * A:
 *   Because only part of region may change permissions.
 *
 *
 * Q: Why merge VMAs?
 *
 * A:
 *   To reduce fragmentation and improve lookup efficiency.
 *
 ******************************************************************************/

/******************************************************************************
 * 13. FINAL BIG PICTURE
 *
 * mprotect.c bridges:
 *
 *      USER REQUEST
 *          ↓
 *      VMA LOGIC (mm layer)
 *          ↓
 *      PAGE TABLE UPDATE (hardware layer)
 *          ↓
 *      TLB CONSISTENCY
 *
 *
 * This is a perfect example of:
 *
 *      high-level memory management + low-level hardware interaction
 *
 ******************************************************************************/
