================================================================================
FILE: linux_hugetlb_background_and_flow.txt
TOPIC: Generic HugeTLB Support, Huge Page Pool Management, Fault Handling,
       COW, Unmap, Protection Changes, and Reservation Accounting
SOURCE: mm/hugetlb.c
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHY HUGETLB EXISTS
================================================================================

Normal Linux memory management usually works with base pages:

    PAGE_SIZE
    e.g. 4 KB on many systems

But some workloads benefit from much larger pages, called:

    huge pages

Examples of benefits:

    fewer TLB entries needed
    lower TLB miss rate
    better performance for large memory regions
    reduced page-table overhead

HugeTLB is the Linux subsystem for managing a reserved pool of huge pages.

Important idea:

    these are not transparent huge pages
    these are explicitly managed huge pages from a preallocated pool

So this code manages:

    creation of a huge page pool
    allocation/free from that pool
    huge page faults
    huge page COW handling
    huge page unmapping
    huge page protection changes
    reservation accounting



SECTION 2: WHAT KIND OF "HUGE PAGE" THIS CODE HANDLES
================================================================================

This code deals with HugeTLB pages whose size is:

    HPAGE_SIZE

and whose shift/order are:

    HPAGE_SHIFT
    HUGETLB_PAGE_ORDER

So one huge page is physically composed of multiple normal pages:

    HPAGE_SIZE / PAGE_SIZE

Example mental model:

    if PAGE_SIZE  = 4 KB
    and HPAGE_SIZE = 2 MB

then one huge page contains:

    2 MB / 4 KB = 512 base pages

This is why helper loops often iterate:

    for (i = 0; i < HPAGE_SIZE / PAGE_SIZE; i++)



SECTION 3: BIG PICTURE
================================================================================

This file has several major responsibilities:

A) Huge page pool management

    alloc_fresh_huge_page()
    alloc_huge_page()
    free_huge_page()
    enqueue_huge_page()
    dequeue_huge_page()
    hugetlb_init()
    set_max_huge_pages()


B) Huge page content operations

    clear_huge_page()
    copy_huge_page()


C) Huge page fault handling

    hugetlb_fault()
    hugetlb_no_page()
    hugetlb_cow()


D) Huge page mapping/unmapping/protection

    make_huge_pte()
    set_huge_ptep_writable()
    copy_hugetlb_page_range()
    __unmap_hugepage_range()
    unmap_hugepage_range()
    hugetlb_change_protection()
    follow_hugetlb_page()


E) Huge page reservation accounting

    hugetlb_acct_memory()
    hugetlb_reserve_pages()
    hugetlb_unreserve_pages()
    region_add()
    region_chg()
    region_truncate()


So this file is both:

    allocator/pool manager
and
    MM fault/mapping logic
and
    reservation bookkeeping layer



SECTION 4: GLOBAL STATE
================================================================================

Important globals:

    const unsigned long hugetlb_zero = 0
    const unsigned long hugetlb_infinity = ~0UL

These are symbolic values used elsewhere for policies/limits.


Core counters:

    static unsigned long nr_huge_pages
        total huge pages currently in pool

    static unsigned long free_huge_pages
        huge pages currently free on freelists

    static unsigned long resv_huge_pages
        huge pages reserved for future allocation

    unsigned long max_huge_pages
        target configured pool size


Per-node state:

    static struct list_head hugepage_freelists[MAX_NUMNODES]
        free huge pages per NUMA node

    static unsigned int nr_huge_pages_node[MAX_NUMNODES]
        total huge pages per node

    static unsigned int free_huge_pages_node[MAX_NUMNODES]
        free huge pages per node


Lock:

    static DEFINE_SPINLOCK(hugetlb_lock)

This protects:

    hugepage_freelists
    nr_huge_pages
    free_huge_pages
    related per-node counters
    reservation accounting access to shared pool state



SECTION 5: WHY THERE IS A PREALLOCATED POOL
================================================================================

HugeTLB pages are generally not allocated on every fault in the same lazy,
best-effort way as ordinary base pages.

Instead Linux keeps a dedicated pool of huge pages.

Why?

Because allocating a huge physically contiguous compound page is much harder
than allocating one base page.

If the system waited until the last moment, failures would be frequent due to:

    fragmentation
    lack of contiguous memory
    strict huge page semantics

So HugeTLB pool design is:

    reserve and manage a dedicated supply of huge pages

Then faults and mappings use that supply.



SECTION 6: clear_huge_page() AND copy_huge_page()
================================================================================

clear_huge_page(page, addr)

Purpose:

    zero the contents of an entire huge page

Flow:

    for i in 0 .. (HPAGE_SIZE/PAGE_SIZE)-1:
        cond_resched()
        clear_user_highpage(page + i, addr)

So it iterates base-page by base-page through the compound huge page.

Why cond_resched() and might_sleep()?

Because a huge page can be large, and clearing it may take noticeable time.
The kernel allows rescheduling during the loop.


copy_huge_page(dst, src, addr, vma)

Purpose:

    copy contents of one huge page to another, base page by base page

Flow:

    for i in 0 .. (HPAGE_SIZE/PAGE_SIZE)-1:
        cond_resched()
        copy_user_highpage(dst + i, src + i, addr + i*PAGE_SIZE, vma)

This is mainly used for hugepage COW.



SECTION 7: FREE LIST MANAGEMENT
================================================================================

enqueue_huge_page(page)

    put a huge page onto its NUMA-node free list

Flow:

    nid = page_to_nid(page)
    list_add(&page->lru, &hugepage_freelists[nid])
    free_huge_pages++
    free_huge_pages_node[nid]++

So free huge pages live on per-node freelists.


dequeue_huge_page(vma, address)

    remove one suitable huge page from a free list

It chooses a node using:

    numa_node_id()
    huge_zonelist(vma, address)
    zone_to_nid()
    cpuset_zone_allowed_softwall()

Then if a node has free huge pages:

    page = first entry on hugepage_freelists[nid]
    list_del(&page->lru)
    free_huge_pages--
    free_huge_pages_node[nid]--

So allocation is NUMA-aware and cpuset-aware.



SECTION 8: WHY dequeue_huge_page() USES huge_zonelist() AND CPUSET CHECKS
================================================================================

Huge page allocation should respect memory placement policies as much as possible.

So the code does not just grab from any arbitrary node.

It considers:

    huge_zonelist(vma, address)
        which nodes/zones are preferred for this VMA/address

    cpuset_zone_allowed_softwall(...)
        whether cpuset policy allows allocation from that zone

So HugeTLB pool allocation tries to preserve:

    NUMA locality
    cpuset constraints

even though the pool itself is globally managed.



SECTION 9: free_huge_page()
================================================================================

Function:

    static void free_huge_page(struct page *page)

Purpose:

    return a huge page to the HugeTLB allocator free pool

Flow:

    BUG_ON(page_count(page));

    INIT_LIST_HEAD(&page->lru);

    spin_lock(&hugetlb_lock);
    enqueue_huge_page(page);
    spin_unlock(&hugetlb_lock);

Important detail:

    page_count(page) must be zero before returning to pool

So this is not freeing to the normal buddy allocator.
It is returning to the hugepage free list.

This function is also used as the compound page destructor target.



SECTION 10: alloc_fresh_huge_page()
================================================================================

Function:

    static int alloc_fresh_huge_page(void)

Purpose:

    allocate a brand new huge page from normal page allocator,
    convert it into a HugeTLB pool page, and return it into the hugepage pool

Flow:

    page = alloc_pages_node(nid,
                            GFP_HIGHUSER | __GFP_COMP | __GFP_NOWARN,
                            HUGETLB_PAGE_ORDER);

Then round-robin advance nid across online nodes.

If allocation succeeded:

    set_compound_page_dtor(page, free_huge_page);

    spin_lock(&hugetlb_lock);
    nr_huge_pages++;
    nr_huge_pages_node[page_to_nid(page)]++;
    spin_unlock(&hugetlb_lock);

    put_page(page);   /* free it into the hugepage allocator */

    return 1

Else:

    return 0

Important point:

    alloc_pages_node(...) allocates the compound huge page from buddy
    then put_page(page) releases it so the hugepage destructor path moves it
    into HugeTLB free pool

So a freshly allocated huge page becomes part of the reserved hugepage pool.



SECTION 11: WHY __GFP_COMP IS USED
================================================================================

Huge pages are compound pages.

That means one head page represents a group of base pages acting as one logical
large page.

The flag:

    __GFP_COMP

tells the allocator/setup logic to treat the allocation as a compound page.

That is essential because later code expects huge pages to behave as one unit
for mapping/refcount/COW purposes.



SECTION 12: alloc_huge_page()
================================================================================

Function:

    static struct page *alloc_huge_page(struct vm_area_struct *vma,
                                        unsigned long addr)

Purpose:

    allocate one huge page from the HugeTLB free pool for actual use

Flow:

    spin_lock(&hugetlb_lock);

    if (vma->vm_flags & VM_MAYSHARE)
        resv_huge_pages--;
    else if (free_huge_pages <= resv_huge_pages)
        goto fail;

    page = dequeue_huge_page(vma, addr);
    if (!page)
        goto fail;

    spin_unlock(&hugetlb_lock);
    set_page_refcounted(page);
    return page;

fail:
    spin_unlock(&hugetlb_lock);
    return NULL

Key idea:

Shared mappings and private mappings interact differently with reservations.

For shared mappings:

    consume one reservation immediately

For private mappings:

    allocation must not consume pages reserved for others, so if:

        free_huge_pages <= resv_huge_pages

    allocation fails

After successful dequeue:

    set_page_refcounted(page)

so it becomes live/in-use again.



SECTION 13: WHAT resv_huge_pages MEANS
================================================================================

This counter represents huge pages that are not currently allocated,
but are promised/reserved for future faults/instantiations.

So:

    free_huge_pages
        physically available on free list

    resv_huge_pages
        subset conceptually spoken for

Therefore private allocation path checks:

    if free_huge_pages <= resv_huge_pages
        fail

because otherwise it would steal huge pages reserved for some other mapping.

Reservation accounting is very important in HugeTLB because mappings often want
guarantees that future faults will succeed.



SECTION 14: hugetlb_init() AND hugepages= BOOT PARAMETER
================================================================================

hugetlb_setup(char *s)

    parses boot parameter:

        hugepages=<N>

and sets:

    max_huge_pages = N


hugetlb_init()

Purpose:

    initialize HugeTLB subsystem and preallocate configured pool

Flow:

    if (HPAGE_SHIFT == 0)
        return 0

    INIT_LIST_HEAD for each node freelist

    for i in 0 .. max_huge_pages-1:
        if !alloc_fresh_huge_page()
            break;

    max_huge_pages = free_huge_pages = nr_huge_pages = i;

    printk("Total HugeTLB memory allocated, %ld\n", free_huge_pages);

So at boot:

    create per-node freelists
    allocate up to configured number of huge pages
    place them into HugeTLB pool



SECTION 15: SYSCTL RESIZING OF HUGE PAGE POOL
================================================================================

Under CONFIG_SYSCTL, this code allows runtime adjustment of huge page pool size.

Main helpers:

    update_and_free_page()
    try_to_free_low()
    set_max_huge_pages()
    hugetlb_sysctl_handler()

update_and_free_page(page)

    remove a huge page from HugeTLB accounting and free it back to buddy

It:

    nr_huge_pages--
    nr_huge_pages_node[...]--
    clears selected page flags across all subpages
    set_page_refcounted(page)
    __free_pages(page, HUGETLB_PAGE_ORDER)

So this is true destruction of a huge page from the pool.


try_to_free_low(count)

Under CONFIG_HIGHMEM, it tries to free non-highmem huge pages first.
This is a policy preference to preserve lowmem pressure behavior.


set_max_huge_pages(count)

If growing:
    repeatedly alloc_fresh_huge_page()

If shrinking:
    count = max(count, resv_huge_pages)
    try_to_free_low(count)
    while count < nr_huge_pages:
        page = dequeue_huge_page(NULL, 0)
        update_and_free_page(page)

Meaning:

    cannot shrink below current reserved count
    only free pages can actually be removed
    prefers freeing lowmem pages first when applicable


hugetlb_sysctl_handler()

    updates max_huge_pages from sysctl
    then applies it via set_max_huge_pages()



SECTION 16: MEMINFO REPORTING
================================================================================

Functions:

    hugetlb_report_meminfo()
    hugetlb_report_node_meminfo()
    hugetlb_total_pages()

These export stats such as:

    HugePages_Total
    HugePages_Free
    HugePages_Rsvd
    Hugepagesize

hugetlb_total_pages()

    returns total HugeTLB memory in base-page units:

        nr_huge_pages * (HPAGE_SIZE / PAGE_SIZE)

So this converts pool size from "huge page count" to "normal page count".



SECTION 17: hugetlb_nopage() AND hugetlb_vm_ops
================================================================================

Function:

    static struct page *hugetlb_nopage(...)

It just:

    BUG();

Comment explains why:

    normal handle_mm_fault()/nopage logic for base pages should never be used
    for hugetlb mappings

HugeTLB has separate fault handling, not ordinary page fault instantiation.

So if generic nopage path reaches here for hugetlb:

    it is a bug

vm ops:

    struct vm_operations_struct hugetlb_vm_ops = {
        .nopage = hugetlb_nopage,
    };

This is mostly a guardrail, not the real fault path.



SECTION 18: make_huge_pte()
================================================================================

Function:

    static pte_t make_huge_pte(struct vm_area_struct *vma,
                               struct page *page,
                               int writable)

Purpose:

    construct a huge-page PTE entry

Flow:

    if writable:
        entry = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
    else:
        entry = pte_wrprotect(mk_pte(page, vma->vm_page_prot));

    entry = pte_mkyoung(entry);
    entry = pte_mkhuge(entry);

Meaning:

    build base PTE from page + VMA protections
    maybe writable+dirty
    mark accessed/young
    mark huge

So this is the helper used whenever installing a HugeTLB PTE.



SECTION 19: set_huge_ptep_writable()
================================================================================

Function:

    static void set_huge_ptep_writable(...)

Purpose:

    upgrade an existing huge PTE to writable+dirty

Flow:

    entry = pte_mkwrite(pte_mkdirty(*ptep));
    ptep_set_access_flags(vma, address, ptep, entry, 1);
    update_mmu_cache(vma, address, entry);
    lazy_mmu_prot_update(entry);

This is used in hugepage COW optimization when we can avoid copying and just
make the page writable in place.



SECTION 20: copy_hugetlb_page_range()
================================================================================

Purpose:

    duplicate hugepage mappings from parent mm to child mm during fork

Inputs:

    dst mm
    src mm
    vma

Flow for each huge page address in VMA:

    src_pte = huge_pte_offset(src, addr)
    if absent:
        continue

    dst_pte = huge_pte_alloc(dst, addr)
    if allocation fails:
        return -ENOMEM

    lock dst and src page tables

    if src pte present:
        if COW needed:
            ptep_set_wrprotect(src, addr, src_pte)

        entry = *src_pte
        ptepage = pte_page(entry)
        get_page(ptepage)
        set_huge_pte_at(dst, addr, dst_pte, entry)

COW condition:

    cow = (vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE

Meaning:

    private writable mapping -> fork should make it read-only for COW

So this is the hugepage-aware fork-duplication logic.



SECTION 21: __unmap_hugepage_range() AND unmap_hugepage_range()
================================================================================

Purpose:

    remove hugepage mappings in a VMA over [start, end)

Requirements:

    start and end must be hugepage aligned
    VMA must be hugetlb

Flow:

    lock mm->page_table_lock
    for each hugepage-sized address:
        ptep = huge_pte_offset(mm, address)
        if none:
            continue

        if huge_pmd_unshare(...):
            continue

        pte = huge_ptep_get_and_clear(mm, address, ptep)
        if pte_none(pte):
            continue

        page = pte_page(pte)
        list_add(&page->lru, &page_list)

    unlock page_table_lock
    flush_tlb_range(vma, start, end)

    for each page in page_list:
        put_page(page)

Important detail:

    pages are gathered onto a temporary list first, then put_page() later

Why?

Because unmapping same huge page from multiple places could race via page->lru,
so outer wrapper holds:

    vma->vm_file->f_mapping->i_mmap_lock

during unmap_hugepage_range()

Special cleanup note:

    if vma->vm_file is NULL during mmap error path cleanup, do nothing safely.



SECTION 22: HUGEPAGE COW - hugetlb_cow()
================================================================================

Purpose:

    handle write fault on a private read-only hugepage mapping

Flow:

    old_page = pte_page(pte)

Optimization:

    avoidcopy = (page_count(old_page) == 1)

If nobody else uses it:

    set_huge_ptep_writable(vma, address, ptep)
    return VM_FAULT_MINOR

This is the "reuse page, no copy needed" fast path.


Otherwise:

    get ref on old_page
    new_page = alloc_huge_page(vma, address)
    if fail:
        return VM_FAULT_OOM

    unlock page_table_lock
    copy_huge_page(new_page, old_page, ...)
    relock page_table_lock

    ptep = huge_pte_offset(mm, address & HPAGE_MASK)
    if pte still unchanged:
        set_huge_pte_at(mm, address, ptep,
                        make_huge_pte(vma, new_page, 1))
        new_page = old_page   /* so old one gets freed below */

    page_cache_release(new_page)
    page_cache_release(old_page)
    return VM_FAULT_MINOR

So hugepage COW is conceptually same as normal COW:

    if exclusive, just make writable
    else allocate new page, copy contents, replace mapping

But done at hugepage granularity.



SECTION 23: hugetlb_no_page()
================================================================================

Purpose:

    handle fault when hugepage PTE is absent

This is the "instantiate huge page" path.

Major steps:

STEP 1: Determine file mapping and hugepage index

    mapping = vma->vm_file->f_mapping
    idx = ((address - vma->vm_start) >> HPAGE_SHIFT)
          + (vma->vm_pgoff >> (HPAGE_SHIFT - PAGE_SHIFT))

This computes file index in hugepage units.


STEP 2: Retry loop around page cache lookup

    page = find_lock_page(mapping, idx)

If page not found:
    size = i_size_read(mapping->host) >> HPAGE_SHIFT
    if idx >= size:
        SIGBUS

    if hugetlb_get_quota(mapping):
        fail

    page = alloc_huge_page(vma, address)
    if fail:
        hugetlb_put_quota(mapping)
        return VM_FAULT_OOM

    clear_huge_page(page, address)

    if VMA is shared:
        err = add_to_page_cache(page, mapping, idx, GFP_KERNEL)
        if err:
            put_page(page)
            hugetlb_put_quota(mapping)
            if err == -EEXIST:
                goto retry
            fail
    else
        lock_page(page)

So for shared mappings:
    huge page goes into page cache

For private mappings:
    page need not be in page cache same way; page is just locked for use


STEP 3: Install huge PTE

    spin_lock(&mm->page_table_lock)

    re-check file size
    if idx >= size:
        backout

    if pte already not none:
        backout

    new_pte = make_huge_pte(vma, page,
                ((vma->vm_flags & VM_WRITE) &&
                 (vma->vm_flags & VM_SHARED)))

    set_huge_pte_at(mm, address, ptep, new_pte)

For shared writable mappings:
    initial huge PTE may be writable

For private mappings:
    later write fault may trigger COW


STEP 4: Optional write fault optimization for private mapping

    if (write_access && !(vma->vm_flags & VM_SHARED))
        ret = hugetlb_cow(...)

This avoids requiring a second fault after initial instantiation.


STEP 5: Unlock page and return

On backout:
    release quota
    unlock/put page
    return failure path



SECTION 24: hugetlb_fault()
================================================================================

Purpose:

    top-level hugepage fault handler

Flow:

    ptep = huge_pte_alloc(mm, address)
    if fail:
        VM_FAULT_OOM

Then it uses a static mutex:

    static DEFINE_MUTEX(hugetlb_instantiation_mutex);

Why?

Comment explains:

    serialize hugepage allocation and instantiation
    avoid spurious allocation failures when two CPUs race to instantiate
    same page in page cache

So:

    mutex_lock(&hugetlb_instantiation_mutex)

    entry = *ptep
    if pte_none(entry):
        ret = hugetlb_no_page(...)
        unlock mutex
        return ret

Otherwise mapping already exists:

    ret = VM_FAULT_MINOR

    lock mm->page_table_lock
    if pte still same and write_access && !pte_write(entry):
        ret = hugetlb_cow(...)
    unlock page_table_lock

    unlock mutex
    return ret

So hugetlb_fault() handles both:

    missing mapping instantiation
    write-fault-triggered huge COW



SECTION 25: WHY hugetlb_instantiation_mutex EXISTS
================================================================================

Without serialization, two CPUs faulting same huge page could both do:

    no page in page cache seen
    both allocate huge page
    one wins insertion
    one loses
    temporary pool pressure / quota confusion / spurious failures

Huge pages are expensive and scarce, so this mutex prevents such races from
creating avoidable failures.

It serializes:

    allocation + instantiation

for HugeTLB faults.



SECTION 26: follow_hugetlb_page()
================================================================================

Purpose:

    walk hugepage mappings and return normal struct page* array entries,
    one base page at a time

This is used by interfaces that want to "follow" user pages even when backed
by huge pages.

Flow:

    while vaddr < vma->vm_end and remainder:

        pte = huge_pte_offset(mm, vaddr & HPAGE_MASK)

        if missing or none:
            unlock page_table_lock
            ret = hugetlb_fault(mm, vma, vaddr, 0)
            relock
            if ret == VM_FAULT_MINOR:
                continue
            else:
                error / stop

        pfn_offset = (vaddr & ~HPAGE_MASK) >> PAGE_SHIFT
        page = pte_page(*pte)

same_page:
        if pages array provided:
            get_page(page)
            pages[i] = page + pfn_offset

        if vmas array provided:
            vmas[i] = vma

        advance one base PAGE_SIZE at a time
        while still inside same huge page:
            goto same_page

So even though the mapping is one huge page,
the caller can receive an array of base-page pointers covering that huge page.



SECTION 27: hugetlb_change_protection()
================================================================================

Purpose:

    change protection on a hugepage-mapped range

Flow:

    flush_cache_range(vma, address, end)

    lock mapping->i_mmap_lock
    lock mm->page_table_lock

    for each hugepage-sized address in range:
        ptep = huge_pte_offset(mm, address)
        if absent:
            continue
        if huge_pmd_unshare(...):
            continue
        if pte present:
            pte = huge_ptep_get_and_clear(...)
            pte = pte_mkhuge(pte_modify(pte, newprot))
            set_huge_pte_at(mm, address, ptep, pte)
            lazy_mmu_prot_update(pte)

    unlock locks
    flush_tlb_range(vma, start, end)

So this is the hugepage-aware mprotect-style helper.



SECTION 28: RESERVATION REGIONS - BACKGROUND
================================================================================

The bottom part of the file introduces:

    struct file_region {
        struct list_head link;
        long from;
        long to;
    };

These regions track reserved hugepage index intervals for a file/inode.

Why needed?

HugeTLB mappings often want guarantees that future page faults will succeed.

Suppose a file-backed huge mapping is created for a range, but faults happen later.
The system needs to reserve huge pages up front so later faults do not fail
unexpectedly after the mapping was established.

So reservation bookkeeping tracks:

    which file index ranges have huge pages reserved



SECTION 29: region_add(), region_chg(), region_truncate()
================================================================================

These helpers manipulate the inode reservation region list:

    inode->i_mapping->private_list

region_add(head, f, t)

    merge [f, t) into region list
    coalesce overlapping regions


region_chg(head, f, t)

    compute how many NEW huge pages would need to be reserved
    if [f, t) were added
    may create a placeholder zero-size region node if needed

Return value:

    positive delta = additional reservation needed
    negative       = error


region_truncate(head, end)

    truncate reservation list at offset end
    delete later regions
    return how many reserved pages are released


These are interval-list bookkeeping helpers for reservation accounting.



SECTION 30: hugetlb_acct_memory()
================================================================================

Purpose:

    change reservation count if enough free huge pages exist

Flow:

    spin_lock(&hugetlb_lock)
    if ((delta + resv_huge_pages) <= free_huge_pages) {
        resv_huge_pages += delta;
        ret = 0;
    }
    spin_unlock(&hugetlb_lock)

Meaning:

    reservations can only increase if enough free huge pages remain

This ensures promises do not exceed supply.

So resv_huge_pages is not just a hint.
It is capacity accounting against the free pool.



SECTION 31: hugetlb_reserve_pages()
================================================================================

Purpose:

    reserve huge pages for inode file region [from, to)

Flow:

    chg = region_chg(&inode->i_mapping->private_list, from, to)
    if chg < 0:
        return error

    ret = hugetlb_acct_memory(chg)
    if ret < 0:
        return error

    region_add(&inode->i_mapping->private_list, from, to)
    return 0

Meaning:

    1) calculate how many additional reservations are needed
    2) if pool can support it, increment resv_huge_pages
    3) record region in inode reservation list

So this is how HugeTLB establishes future-fault guarantees for file-backed
huge mappings.



SECTION 32: hugetlb_unreserve_pages()
================================================================================

Purpose:

    release reservations when file range is truncated/freed/etc.

Flow:

    chg = region_truncate(&inode->i_mapping->private_list, offset)
    hugetlb_acct_memory(freed - chg)

Interpretation:

    region list is truncated
    some reserved region entries disappear
    adjust global reservation accounting accordingly

So reservation list and resv_huge_pages stay consistent.



SECTION 33: COMPLETE HUGE PAGE POOL FLOW
================================================================================

Boot or sysctl grow request
    |
    v
alloc_fresh_huge_page()
    |
    +--> alloc_pages_node(... HUGETLB_PAGE_ORDER ...)
    +--> set compound page destructor = free_huge_page
    +--> increment nr_huge_pages counters
    +--> put_page(page)
            |
            v
        free_huge_page(page)
            |
            +--> enqueue_huge_page(page)
            +--> free_huge_pages++


Fault-time huge page allocation
    |
    v
alloc_huge_page(vma, addr)
    |
    +--> respect reservation logic
    +--> dequeue_huge_page(vma, addr)
    +--> set_page_refcounted(page)
    +--> return huge page


When last ref drops
    |
    v
free_huge_page(page)
    |
    +--> enqueue_huge_page(page)
    +--> back to HugeTLB pool



SECTION 34: COMPLETE HUGE FAULT FLOW
================================================================================

Process faults in hugetlb VMA
    |
    v
hugetlb_fault(mm, vma, address, write_access)
    |
    +--> huge_pte_alloc(mm, address)
    +--> mutex_lock(hugetlb_instantiation_mutex)
    +--> entry = *ptep
            |
            +--> if pte none:
            |       hugetlb_no_page(...)
            |           |
            |           +--> find_lock_page(mapping, idx)?
            |           |       |
            |           |       +--> hit: reuse existing huge page
            |           |       +--> miss:
            |           |             check i_size
            |           |             get quota
            |           |             alloc_huge_page()
            |           |             clear_huge_page()
            |           |             for shared VMA add_to_page_cache()
            |           |
            |           +--> lock page_table_lock
            |           +--> verify size and empty pte
            |           +--> set_huge_pte_at(...)
            |           +--> if write fault on private:
            |                   hugetlb_cow(...)
            |           +--> unlock page / return
            |
            +--> else pte already present:
                    if write_access and !pte_write(entry):
                        hugetlb_cow(...)
    |
    +--> unlock mutex
    |
    v
return VM_FAULT_* result



SECTION 35: COMPLETE HUGE COW FLOW
================================================================================

Private hugepage write fault
    |
    v
hugetlb_cow(mm, vma, address, ptep, old_pte)
    |
    +--> old_page = pte_page(old_pte)
    +--> if page_count(old_page) == 1:
    |       set_huge_ptep_writable(...)
    |       return VM_FAULT_MINOR
    |
    +--> else:
            get ref old_page
            alloc_huge_page(...)
            unlock page_table_lock
            copy_huge_page(new_page, old_page, ...)
            relock page_table_lock
            if pte unchanged:
                install writable huge pte for new_page
            release refs
            return VM_FAULT_MINOR

This is ordinary COW logic, but at huge-page granularity.



SECTION 36: COMPLETE RESERVATION FLOW
================================================================================

Mapping setup / reservation request
    |
    v
hugetlb_reserve_pages(inode, from, to)
    |
    +--> region_chg(private_list, from, to)
    |       compute extra reservation needed
    |
    +--> hugetlb_acct_memory(chg)
    |       ensure free pool can cover reservation
    |       resv_huge_pages += chg
    |
    +--> region_add(private_list, from, to)
    |
    v
future faults can consume reserved huge pages


Truncate / unmap / cleanup
    |
    v
hugetlb_unreserve_pages(inode, offset, freed)
    |
    +--> region_truncate(private_list, offset)
    +--> hugetlb_acct_memory(freed - chg)
    |
    v
reservation count adjusted back down



SECTION 37: WHY HUGE PAGE FAULTS DIFFER FROM NORMAL PAGE FAULTS
================================================================================

Normal page fault logic deals with:

    base pages
    normal page tables
    ordinary nopage/handle_mm_fault paths

HugeTLB differs because:

    page size is much larger
    PTE/PMD handling is architecture-specific
    pages come from dedicated pool
    reservation guarantees matter
    hugepage COW is expensive and special
    fault path must use huge_pte_* helpers

That is why this file has separate fault handlers rather than reusing normal
base-page VM fault logic.



SECTION 38: SIMPLE MENTAL MODEL
================================================================================

Think of ordinary memory pages as normal parking spots,
and huge pages as giant reserved bus bays.

Normal allocator:

    finds a small parking spot whenever needed

HugeTLB allocator:

    pre-reserves a set of giant bus bays
    tracks which are free
    tracks which are promised for future buses
    assigns a whole bay when a bus arrives
    can duplicate a bay on copy-on-write if necessary

Reservation accounting means:

    some empty bus bays are already promised to future buses,
    so you cannot give them away to someone else now.

That is exactly the role of:

    free_huge_pages
    resv_huge_pages



SECTION 39: SUMMARY
================================================================================

mm/hugetlb.c implements generic HugeTLB support.

Main responsibilities:

    manage dedicated hugepage pool
    allocate/free huge pages from per-node freelists
    preallocate pool at boot or resize via sysctl
    clear/copy huge pages
    instantiate hugepage mappings on faults
    handle hugepage copy-on-write
    copy huge mappings across fork
    unmap hugepage ranges
    change hugepage protections
    track reservations so future faults are guaranteed

Key functions:

    alloc_fresh_huge_page()
        create new huge page for pool

    alloc_huge_page()
        allocate from reserved hugepage pool

    free_huge_page()
        return huge page to pool

    hugetlb_fault()
        top-level hugepage fault handler

    hugetlb_no_page()
        instantiate missing hugepage mapping

    hugetlb_cow()
        handle private write fault / copy-on-write

    copy_hugetlb_page_range()
        fork-time huge mapping duplication

    unmap_hugepage_range()
        tear down huge mappings

    hugetlb_reserve_pages()
    hugetlb_unreserve_pages()
        reservation bookkeeping

Core ideas to remember:

    HugeTLB uses a dedicated preallocated pool, not ordinary lazy best-effort
    base-page allocation
    huge page faults and COW are special and use huge_pte helpers
    reservation accounting protects future fault success
    one huge page is a compound page made of many base pages

In short:

    this file is the generic Linux engine for reserving, allocating, mapping,
    faulting, copying, and accounting explicit HugeTLB pages.


================================================================================
END OF FILE
================================================================================

