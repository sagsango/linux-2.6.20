```c
/*
 * FILE: arch/x86_64/mm/pageattr.c
 *
 * PURPOSE:
 *   Change page-table attributes for pages in the x86-64 kernel
 *   linear/direct mapping.
 *
 * MAIN USE CASE:
 *   Avoid cache attribute conflicts when the same physical memory is
 *   mapped with different cache policies.
 *
 * Example:
 *
 *      ioremap_nocache()
 *          |
 *          v
 *      maps physical memory as uncached
 *          |
 *          v
 *      direct-map alias must also become uncached
 *          |
 *          v
 *      change_page_attr_addr()
 *
 *
 * ============================================================
 * 1. BACKGROUND
 * ============================================================
 *
 * x86-64 kernel has a direct map:
 *
 *      physical RAM
 *          |
 *          v
 *      kernel virtual address
 *
 * Example:
 *
 *      phys 0x00100000
 *          |
 *          v
 *      virt __va(0x00100000)
 *
 * This direct map is created during boot.
 *
 * For performance, it often uses large pages:
 *
 *      2MB PMD mappings
 *
 * instead of many 4KB PTE mappings.
 *
 *
 * Problem:
 *
 *      Sometimes one 4KB page inside a large direct-map region needs a
 *      different page attribute.
 *
 * Example:
 *
 *      normal direct map:
 *          write-back cacheable
 *
 *      ioremap_nocache alias:
 *          uncached
 *
 * Some CPUs do not like one physical page having two aliases with
 * different cache policies.
 *
 * Therefore this file changes the direct-map page attributes too.
 */


/*
 * ============================================================
 * 2. KEY CONCEPTS
 * ============================================================
 *
 * PAGE ATTRIBUTE:
 *
 *      Page-table flags controlling how CPU treats the page.
 *
 * Examples:
 *
 *      _PAGE_PRESENT
 *      _PAGE_RW
 *      _PAGE_PCD        cache disable
 *      _PAGE_PWT        write through
 *      executable / non-executable
 *
 *
 * LARGE PAGE:
 *
 *      A PMD entry maps a large physical range directly.
 *
 *      Usually:
 *
 *          2MB on x86-64
 *
 *
 * SPLITTING LARGE PAGE:
 *
 *      Convert:
 *
 *          one PMD large mapping
 *
 *      into:
 *
 *          one page of 512 PTEs
 *
 *      because:
 *
 *          2MB / 4KB = 512
 *
 *
 * REVERTING:
 *
 *      If no special 4KB attributes remain, convert the 512 PTEs back
 *      into one large PMD mapping.
 */


/*
 * ============================================================
 * 3. HIGH-LEVEL FLOW
 * ============================================================
 *
 *      caller
 *        |
 *        v
 *      change_page_attr_addr(address, numpages, prot)
 *        |
 *        v
 *      for each page:
 *        |
 *        v
 *      __change_page_attr(address, pfn, prot, PAGE_KERNEL)
 *        |
 *        v
 *      lookup_address(address)
 *        |
 *        +-- normal PTE:
 *        |       replace PTE with new attributes
 *        |
 *        +-- large PMD:
 *                split large page into PTE page
 *                modify only target 4KB PTE
 *
 *      later:
 *
 *      global_flush_tlb()
 *        |
 *        v
 *      flush cache/TLB on all CPUs
 *        |
 *        v
 *      free deferred split PTE pages
 */


/*
 * ============================================================
 * 4. lookup_address()
 * ============================================================
 *
 * PURPOSE:
 *   Find the page-table entry for a kernel virtual address.
 */

static inline pte_t *lookup_address(unsigned long address)
{
        pgd_t *pgd = pgd_offset_k(address);
        pud_t *pud;
        pmd_t *pmd;
        pte_t *pte;

        if (pgd_none(*pgd))
                return NULL;

        pud = pud_offset(pgd, address);
        if (!pud_present(*pud))
                return NULL;

        pmd = pmd_offset(pud, address);
        if (!pmd_present(*pmd))
                return NULL;

        if (pmd_large(*pmd))
                return (pte_t *)pmd;

        pte = pte_offset_kernel(pmd, address);
        if (pte && !pte_present(*pte))
                pte = NULL;

        return pte;
}

/*
 * Page-table walk:
 *
 *      address
 *        |
 *        v
 *      PGD
 *        |
 *        v
 *      PUD
 *        |
 *        v
 *      PMD
 *        |
 *        +-- if PMD is large:
 *        |       return PMD as pte_t *
 *        |
 *        v
 *      PTE
 *
 *
 * Important trick:
 *
 *      if (pmd_large(*pmd))
 *          return (pte_t *)pmd;
 *
 * A large PMD is returned as if it were a PTE pointer.
 * Later code checks:
 *
 *      pte_huge(*kpte)
 *
 * to know whether it is really a huge mapping.
 */


/*
 * ============================================================
 * 5. split_large_page()
 * ============================================================
 *
 * PURPOSE:
 *   Split one large mapping into a PTE page.
 */

static struct page *split_large_page(unsigned long address,
                                     pgprot_t prot,
                                     pgprot_t ref_prot)
{
        int i;
        unsigned long addr;
        struct page *base = alloc_pages(GFP_KERNEL, 0);
        pte_t *pbase;

        if (!base)
                return NULL;

        /*
         * page_private tracks how many PTEs inside this split page
         * have non-standard attributes.
         */
        SetPagePrivate(base);
        page_private(base) = 0;

        address = __pa(address);
        addr = address & LARGE_PAGE_MASK;

        pbase = (pte_t *)page_address(base);

        for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE) {
                pbase[i] = pfn_pte(addr >> PAGE_SHIFT,
                                   addr == address ? prot : ref_prot);
        }

        return base;
}

/*
 * What happens here:
 *
 *      1. Allocate one 4KB page.
 *      2. Use that page as a PTE table.
 *      3. Fill 512 PTE entries.
 *      4. One target PTE gets new prot.
 *      5. Other PTEs keep old/default ref_prot.
 *
 *
 * Before:
 *
 *      PMD
 *       |
 *       +--> large 2MB mapping, PAGE_KERNEL
 *
 *
 * After:
 *
 *      PMD
 *       |
 *       +--> PTE page
 *              |
 *              +-- PTE[0]   PAGE_KERNEL
 *              +-- PTE[1]   PAGE_KERNEL
 *              +-- ...
 *              +-- PTE[x]   new prot
 *              +-- ...
 *              +-- PTE[511] PAGE_KERNEL
 */


/*
 * ============================================================
 * 6. cache_flush_page()
 * ============================================================
 *
 * PURPOSE:
 *   Flush one page from CPU caches using clflush.
 */

static void cache_flush_page(void *adr)
{
        int i;

        for (i = 0; i < PAGE_SIZE; i += boot_cpu_data.x86_clflush_size)
                asm volatile("clflush (%0)" :: "r" (adr + i));
}

/*
 * Example:
 *
 *      PAGE_SIZE = 4096
 *      cache line = 64
 *
 * Number of clflush instructions:
 *
 *      4096 / 64 = 64
 *
 *
 * clflush removes that cache line from CPU cache.
 */


/*
 * ============================================================
 * 7. flush_kernel_map()
 * ============================================================
 *
 * PURPOSE:
 *   Flush cache and TLB state for changed kernel mappings on one CPU.
 */

static void flush_kernel_map(void *arg)
{
        struct list_head *l = (struct list_head *)arg;
        struct page *pg;

        /*
         * If clflush is not available, use wbinvd.
         * wbinvd is much heavier: it writes back and invalidates cache.
         */
        if (!cpu_has_clflush)
                asm volatile("wbinvd" ::: "memory");

        list_for_each_entry(pg, l, lru) {
                void *adr = page_address(pg);

                if (cpu_has_clflush)
                        cache_flush_page(adr);

                __flush_tlb_one(adr);
        }
}

/*
 * Why flush?
 *
 * After page-table attributes change:
 *
 *      CPU may still have old TLB entry.
 *      CPU cache may still hold lines with old cache policy.
 *
 * So Linux must flush.
 */


/*
 * ============================================================
 * 8. flush_map()
 * ============================================================
 *
 * PURPOSE:
 *   Run flush_kernel_map() on every CPU.
 */

static inline void flush_map(struct list_head *l)
{
        on_each_cpu(flush_kernel_map, l, 1, 1);
}

/*
 * Why every CPU?
 *
 * Kernel mappings are global.
 * Any CPU may have cached/TLB state for these addresses.
 */


/*
 * ============================================================
 * 9. deferred_pages
 * ============================================================
 */

static LIST_HEAD(deferred_pages); /* protected by init_mm.mmap_sem */

static inline void save_page(struct page *fpage)
{
        list_add(&fpage->lru, &deferred_pages);
}

/*
 * Purpose:
 *
 *      temporarily remember split PTE pages that can be freed later.
 *
 * Why not free immediately?
 *
 *      CPUs may still have stale TLB/cache state.
 *
 * Correct order:
 *
 *      1. remove/revert mapping
 *      2. put old PTE page on deferred list
 *      3. flush all CPUs
 *      4. free old PTE page
 */


/*
 * ============================================================
 * 10. revert_page()
 * ============================================================
 *
 * PURPOSE:
 *   Convert split PTE mapping back to one large page.
 */

static void revert_page(unsigned long address, pgprot_t ref_prot)
{
        pgd_t *pgd;
        pud_t *pud;
        pmd_t *pmd;
        pte_t large_pte;

        pgd = pgd_offset_k(address);
        BUG_ON(pgd_none(*pgd));

        pud = pud_offset(pgd, address);
        BUG_ON(pud_none(*pud));

        pmd = pmd_offset(pud, address);
        BUG_ON(pmd_val(*pmd) & _PAGE_PSE);

        large_pte = mk_pte_phys(__pa(address) & LARGE_PAGE_MASK, ref_prot);
        large_pte = pte_mkhuge(large_pte);

        set_pte((pte_t *)pmd, large_pte);
}

/*
 * When is this called?
 *
 *      When all special PTEs inside the split page are restored.
 *
 * page_private(kpte_page) == 0 means:
 *
 *      no non-standard attributes remain
 *
 *
 * Before:
 *
 *      PMD -> PTE page -> 512 small entries
 *
 * After:
 *
 *      PMD -> one large mapping
 *
 *
 * Why revert?
 *
 *      Large pages reduce TLB pressure and improve performance.
 */


/*
 * ============================================================
 * 11. __change_page_attr()
 * ============================================================
 *
 * PURPOSE:
 *   Change one page's attribute.
 */

static int
__change_page_attr(unsigned long address,
                   unsigned long pfn,
                   pgprot_t prot,
                   pgprot_t ref_prot)
{
        pte_t *kpte;
        struct page *kpte_page;
        pgprot_t ref_prot2;

        kpte = lookup_address(address);
        if (!kpte)
                return 0;

        kpte_page = virt_to_page(((unsigned long)kpte) & PAGE_MASK);

        if (pgprot_val(prot) != pgprot_val(ref_prot)) {
                /*
                 * Change from default attribute to special attribute.
                 */

                if (!pte_huge(*kpte)) {
                        /*
                         * Already a normal 4KB PTE.
                         * Just replace it.
                         */
                        set_pte(kpte, pfn_pte(pfn, prot));
                } else {
                        /*
                         * Large mapping.
                         * Need to split first.
                         */
                        struct page *split;

                        ref_prot2 = pte_pgprot(pte_clrhuge(*kpte));
                        split = split_large_page(address, prot, ref_prot2);

                        if (!split)
                                return -ENOMEM;

                        /*
                         * Replace huge PMD entry with pointer to new PTE page.
                         */
                        set_pte(kpte, mk_pte(split, ref_prot2));
                        kpte_page = split;
                }

                /*
                 * Count one more non-standard PTE in this PTE page.
                 */
                page_private(kpte_page)++;

        } else if (!pte_huge(*kpte)) {
                /*
                 * Change back to default attribute.
                 */

                set_pte(kpte, pfn_pte(pfn, ref_prot));

                BUG_ON(page_private(kpte_page) == 0);

                page_private(kpte_page)--;

        } else {
                BUG();
        }

        /*
         * On x86-64, direct mapping set at boot is not using 4KB pages.
         * So the page-table page should not be reserved.
         */
        BUG_ON(PageReserved(kpte_page));

        /*
         * If no special PTEs remain, restore large page mapping.
         */
        if (page_private(kpte_page) == 0) {
                save_page(kpte_page);
                revert_page(address, ref_prot);
        }

        return 0;
}

/*
 * Main cases:
 *
 * ------------------------------------------------------------
 * CASE 1: prot != ref_prot
 * ------------------------------------------------------------
 *
 * Meaning:
 *
 *      make page special
 *
 * Example:
 *
 *      PAGE_KERNEL
 *          ↓
 *      PAGE_KERNEL | _PAGE_PCD
 *
 *
 * If mapping is 4KB:
 *
 *      change PTE directly
 *
 * If mapping is huge:
 *
 *      split large page
 *      change only target PTE
 *
 * Then:
 *
 *      page_private++
 *
 *
 * ------------------------------------------------------------
 * CASE 2: prot == ref_prot
 * ------------------------------------------------------------
 *
 * Meaning:
 *
 *      restore page back to normal
 *
 * Example:
 *
 *      PAGE_KERNEL | _PAGE_PCD
 *          ↓
 *      PAGE_KERNEL
 *
 * Then:
 *
 *      page_private--
 *
 * If page_private becomes zero:
 *
 *      revert to large page
 */


/*
 * ============================================================
 * 12. change_page_attr_addr()
 * ============================================================
 *
 * PURPOSE:
 *   Change attributes for a range in the kernel linear map.
 */

int change_page_attr_addr(unsigned long address,
                          int numpages,
                          pgprot_t prot)
{
        int err = 0;
        int i;

        down_write(&init_mm.mmap_sem);

        for (i = 0; i < numpages; i++, address += PAGE_SIZE) {
                unsigned long pfn = __pa(address) >> PAGE_SHIFT;

                err = __change_page_attr(address, pfn, prot, PAGE_KERNEL);
                if (err)
                        break;

                /*
                 * Also handle kernel text mapping alias.
                 */
                if (__pa(address) < KERNEL_TEXT_SIZE) {
                        unsigned long addr2;
                        pgprot_t prot2;

                        addr2 = __START_KERNEL_map + __pa(address);

                        /*
                         * Kernel text mapping must stay executable.
                         */
                        prot2 = pte_pgprot(pte_mkexec(pfn_pte(0, prot)));

                        err = __change_page_attr(addr2,
                                                 pfn,
                                                 prot2,
                                                 PAGE_KERNEL_EXEC);
                }
        }

        up_write(&init_mm.mmap_sem);

        return err;
}

/*
 * Important:
 *
 *      This function does NOT flush TLB itself.
 *
 * Caller must call:
 *
 *      global_flush_tlb()
 *
 *
 * Why lock init_mm.mmap_sem?
 *
 *      Protects kernel page-table changes and deferred_pages list.
 *
 *
 * Why second mapping?
 *
 * On x86-64, low kernel physical memory may be mapped through:
 *
 *      1. direct map
 *      2. kernel text mapping
 *
 * So both aliases must remain consistent.
 */


/*
 * ============================================================
 * 13. change_page_attr()
 * ============================================================
 *
 * PURPOSE:
 *   Wrapper when caller has struct page *.
 */

int change_page_attr(struct page *page, int numpages, pgprot_t prot)
{
        unsigned long addr = (unsigned long)page_address(page);
        return change_page_attr_addr(addr, numpages, prot);
}

/*
 * Warning:
 *
 *      Do not call this for MMIO areas that may not have a mem_map entry.
 *
 * Why?
 *
 *      MMIO/device memory may not have struct page.
 *
 * For raw physical/MMIO style work, use:
 *
 *      change_page_attr_addr()
 */


/*
 * ============================================================
 * 14. global_flush_tlb()
 * ============================================================
 *
 * PURPOSE:
 *   Globally flush cache/TLB and free deferred split PTE pages.
 */

void global_flush_tlb(void)
{
        struct page *pg, *next;
        struct list_head l;

        down_read(&init_mm.mmap_sem);
        list_replace_init(&deferred_pages, &l);
        up_read(&init_mm.mmap_sem);

        flush_map(&l);

        list_for_each_entry_safe(pg, next, &l, lru) {
                ClearPagePrivate(pg);
                __free_page(pg);
        }
}

/*
 * Flow:
 *
 *      global_flush_tlb()
 *          |
 *          v
 *      move deferred_pages into local list
 *          |
 *          v
 *      flush_map(&l)
 *          |
 *          v
 *      on every CPU:
 *          |
 *          +-- clflush page or wbinvd
 *          +-- flush TLB entry
 *          |
 *          v
 *      free old split PTE pages
 *
 *
 * Why deferred freeing?
 *
 *      Old PTE page cannot be freed until all CPUs stop using old
 *      translations.
 */


/*
 * ============================================================
 * 15. EXPORTS
 * ============================================================
 */

EXPORT_SYMBOL(change_page_attr);
EXPORT_SYMBOL(global_flush_tlb);

/*
 * These are exported so other kernel code/modules can request page
 * attribute changes and flush them.
 */


/*
 * ============================================================
 * 16. FULL EXAMPLE: UNCACHED IOREMAP
 * ============================================================
 *
 * Driver:
 *
 *      base = ioremap_nocache(phys, size);
 *
 *
 * ioremap code:
 *
 *      __ioremap(phys, size, _PAGE_PCD)
 *
 *
 * Then:
 *
 *      ioremap_change_attr(phys, size, _PAGE_PCD)
 *
 *
 * That calls:
 *
 *      change_page_attr_addr(__va(phys), npages, PAGE_KERNEL | _PAGE_PCD)
 *
 *
 * This file:
 *
 *      1. finds kernel direct-map entry
 *      2. if large page, splits it
 *      3. changes target 4KB page to uncached
 *      4. records non-standard PTE count
 *
 *
 * Caller later:
 *
 *      global_flush_tlb()
 *
 *
 * Result:
 *
 *      ioremap mapping:
 *          uncached
 *
 *      direct map alias:
 *          uncached
 *
 * No cache attribute conflict.
 */


/*
 * ============================================================
 * 17. FULL FLOW DIAGRAM
 * ============================================================
 *
 *      change_page_attr_addr()
 *          |
 *          v
 *      lock init_mm.mmap_sem
 *          |
 *          v
 *      loop over pages
 *          |
 *          v
 *      __change_page_attr()
 *          |
 *          v
 *      lookup_address()
 *          |
 *          +-- missing:
 *          |       return success
 *          |
 *          +-- 4KB PTE:
 *          |       set new PTE
 *          |
 *          +-- huge PMD:
 *                  split_large_page()
 *                  replace PMD with PTE page
 *                  set target PTE
 *
 *          |
 *          v
 *      update page_private count
 *          |
 *          v
 *      if count == 0:
 *          |
 *          +-- save_page()
 *          +-- revert_page()
 *
 *          |
 *          v
 *      unlock
 *
 *          |
 *          v
 *      global_flush_tlb()
 *          |
 *          v
 *      flush on all CPUs
 *          |
 *          v
 *      free deferred PTE pages
 */


/*
 * ============================================================
 * 18. FINAL MENTAL MODEL
 * ============================================================
 *
 * This file manages the difficult case:
 *
 *      "I need to change attributes for one 4KB page, but the kernel
 *       direct map uses large pages."
 *
 * It solves that by:
 *
 *      splitting large pages when needed
 *      changing individual PTEs
 *      counting special PTEs
 *      reverting back to large pages when possible
 *      flushing cache/TLB globally
 *      freeing temporary page-table pages safely
 *
 *
 * One-line summary:
 *
 *      pageattr.c safely changes x86-64 kernel direct-map page attributes,
 *      especially for cache-policy consistency with ioremap and other
 *      special mappings.
 */
```

