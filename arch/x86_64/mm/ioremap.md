FILE: arch/x86_64/mm/ioremap.c
TOPIC: Mapping physical MMIO / device memory into kernel virtual address space

============================================================
1. BIG IDEA
============================================================

ioremap() is used when the kernel wants to access device memory.

Example:

    PCI device BAR physical address = 0xfebf0000

CPU cannot safely access that with a normal pointer directly.

So driver does:

    void __iomem *base;

    base = ioremap_nocache(0xfebf0000, 4096);

    writel(value, base + REG_OFFSET);
    value = readl(base + REG_OFFSET);

    iounmap(base);

ioremap creates a kernel virtual mapping:

    kernel virtual address  --->  physical device/MMIO address

Example:

    0xffffc20000010000  --->  0x00000000febf0000

============================================================
2. WHY IOREMAP IS NEEDED
============================================================

Normal RAM is already mapped by the kernel direct map:

    physical RAM 0x100000
        |
        v
    kernel virtual __va(0x100000)

But device MMIO regions may be:

    - outside normal RAM
    - PCI BAR memory
    - APIC / HPET / framebuffer
    - device control registers
    - high physical addresses

So the kernel creates a special virtual mapping in vmalloc/ioremap space.

============================================================
3. MEMORY TYPES
============================================================

There are two major physical memory categories:

    Normal RAM:
        real system memory
        cacheable
        managed by buddy allocator
        has struct page

    MMIO/device memory:
        device registers or device memory
        often uncached
        not normal RAM
        may not have struct page
        accessed using readl/writel/readb/writeb

============================================================
4. HIGH-LEVEL IOREMAP FLOW
============================================================

Driver:

    base = ioremap_nocache(phys, size);

Kernel:

    validate address
    reject bad RAM mappings
    align physical address
    allocate vmalloc virtual area
    create page table entries
    mark mapping uncached
    return virtual __iomem pointer

Diagram:

    Driver
      |
      v
    ioremap_nocache(phys, size)
      |
      v
    __ioremap(phys, size, _PAGE_PCD)
      |
      +--> validate size / wraparound
      |
      +--> handle ISA low memory special case
      |
      +--> reject normal RAM if not reserved
      |
      +--> build pgprot flags
      |
      +--> page-align address and size
      |
      +--> get_vm_area()
      |
      +--> ioremap_page_range()
      |
      +--> maybe change direct-map cache attributes
      |
      v
    return virtual address

============================================================
5. IMPORTANT CONSTANTS
============================================================

Code:

    #define ISA_START_ADDRESS 0xa0000
    #define ISA_END_ADDRESS   0x100000

Meaning:

    0xa0000 - 0x100000

This is the old PC ISA area:

    0x000a0000 - 0x000bffff    VGA memory
    0x000c0000 - 0x000fffff    BIOS / option ROM area

The comment says:

    "Don't remap the low PCI/ISA area, it's always mapped."

So if the requested physical address is inside this area,
the kernel simply returns:

    phys_to_virt(phys_addr)

instead of creating a new vmalloc mapping.

============================================================
6. FUNCTION: ioremap_change_attr()
============================================================

Code:

    static int ioremap_change_attr(unsigned long phys_addr,
                                   unsigned long size,
                                   unsigned long flags)

Purpose:

    Fix cache attributes of the existing direct mapping.

Why?

On x86-64, normal RAM may already be mapped in the direct map.

Example:

    physical 0x100000
        |
        +--> direct map virtual address
        |
        +--> ioremap virtual address

If one mapping is cacheable and another mapping is uncached, CPU cache
attribute conflicts can happen.

So this function changes the direct-map page attributes too.

============================================================
7. ioremap_change_attr() WALK
============================================================

Code:

    if (phys_addr + size - 1 < (end_pfn_map << PAGE_SHIFT)) {

Meaning:

    If this physical range is inside the kernel direct-map range,
    then the direct mapping already exists.

Then:

    npages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

This calculates number of pages.

Example:

    size = 4096  -> 1 page
    size = 8192  -> 2 pages
    size = 5000  -> 2 pages

Then:

    vaddr = (unsigned long)__va(phys_addr);

Convert physical address to direct-map virtual address.

Then:

    change_page_attr_addr(vaddr, npages,
        __pgprot(__PAGE_KERNEL | flags));

This changes page table attributes for that direct mapping.

Example for nocache:

    flags = _PAGE_PCD

So the direct map also becomes uncached.

Then:

    global_flush_tlb();

Because page table attributes changed.

The CPU may have old translations cached in TLB.

============================================================
8. WHY NOT USE STRUCT PAGE?
============================================================

Comment:

    Must use an address here and not struct page because the phys addr
    can be in a hole between nodes and not have a memmap entry.

Meaning:

    Some physical addresses are not normal RAM.

They may not have:

    struct page

So code cannot safely do:

    virt_to_page()
    pfn_to_page()

for every physical address.

Therefore it works using raw address.

============================================================
9. FUNCTION: __ioremap()
============================================================

Signature:

    void __iomem *__ioremap(unsigned long phys_addr,
                            unsigned long size,
                            unsigned long flags)

Inputs:

    phys_addr:
        physical/bus address to map

    size:
        number of bytes to map

    flags:
        page table cache flags

Example:

    ioremap_nocache(phys, size)
        |
        v
    __ioremap(phys, size, _PAGE_PCD)

_PAGE_PCD means:

    Page Cache Disable

So CPU should not cache accesses to that memory.

============================================================
10. __iomem MEANING
============================================================

Return type:

    void __iomem *

__iomem is a sparse annotation.

It tells kernel developers/tools:

    This pointer points to I/O memory, not normal RAM.

You should access it with:

    readb/readw/readl/readq
    writeb/writew/writel/writeq

Not normal:

    *ptr = value;

============================================================
11. __ioremap(): ZERO SIZE / WRAP CHECK
============================================================

Code:

    last_addr = phys_addr + size - 1;
    if (!size || last_addr < phys_addr)
        return NULL;

Purpose:

    reject size == 0
    reject integer overflow

Example bad case:

    phys_addr = 0xfffffffffffff000
    size      = 0x3000

phys_addr + size wraps around to low address.

So:

    last_addr < phys_addr

That means overflow happened.

============================================================
12. __ioremap(): ISA SPECIAL CASE
============================================================

Code:

    if (phys_addr >= ISA_START_ADDRESS && last_addr < ISA_END_ADDRESS)
        return (__force void __iomem *)phys_to_virt(phys_addr);

Meaning:

    If address is fully inside 0xa0000 - 0x100000,
    return existing mapping.

No new page tables.
No vmalloc area.
No iounmap needed practically.

Diagram:

    phys 0xa0000
       |
       v
    already mapped kernel virtual address

============================================================
13. __ioremap(): DO NOT MAP NORMAL RAM
============================================================

Code:

#ifdef CONFIG_FLATMEM
    if (last_addr < virt_to_phys(high_memory)) {
        ...
        for(page = virt_to_page(t_addr);
            page <= virt_to_page(t_end);
            page++)
            if(!PageReserved(page))
                return NULL;
    }
#endif

Meaning:

    If requested physical range is normal RAM,
    reject it unless the pages are reserved.

Why?

Because mapping normal RAM as device memory can create cache aliasing,
memory corruption, and confusing ownership.

Normal RAM should be accessed through normal kernel mappings, not ioremap.

But reserved pages are allowed.

Example reserved memory:

    - BIOS areas
    - device memory holes
    - special firmware memory
    - framebuffer
    - ACPI tables maybe

Flow:

    requested range below high_memory?
        |
        v
    yes, probably normal RAM
        |
        v
    check every page
        |
        +--> if PageReserved(page)
        |       allowed
        |
        +--> if not reserved
                reject

============================================================
14. HIGH_MEMORY MEANING
============================================================

high_memory marks the end of directly mapped normal RAM.

Check:

    last_addr < virt_to_phys(high_memory)

means:

    requested physical address lies inside normal RAM direct-map range

So kernel is careful.

============================================================
15. PAGE PROTECTION FLAGS
============================================================

Code:

    pgprot = __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_GLOBAL
              | _PAGE_DIRTY | _PAGE_ACCESSED | flags);

This builds page table flags.

Meaning:

    _PAGE_PRESENT:
        PTE is valid

    _PAGE_RW:
        writable

    _PAGE_GLOBAL:
        global TLB entry, not flushed on context switch

    _PAGE_DIRTY:
        page marked dirty

    _PAGE_ACCESSED:
        page marked accessed

    flags:
        caller-specific cache attributes, for example _PAGE_PCD

For ioremap_nocache:

    flags = _PAGE_PCD

So final mapping is:

    present + writable + global + dirty + accessed + cache-disabled

============================================================
16. NON-PAGE-ALIGNED MAPPING
============================================================

Important comment:

    We need to allow non-page-aligned mappings too.

Example:

    phys_addr = 0xfebf0123
    size      = 100

Page tables can only map whole pages.

So kernel maps:

    physical page base = 0xfebf0000

Then returns virtual address with offset:

    returned = mapped_virtual_base + 0x123

Code:

    offset = phys_addr & ~PAGE_MASK;
    phys_addr &= PAGE_MASK;
    size = PAGE_ALIGN(last_addr + 1) - phys_addr;

Example:

    original phys_addr = 0xfebf0123
    size               = 100

    offset             = 0x123
    aligned phys_addr  = 0xfebf0000
    aligned size       = 4096

Return:

    addr + offset

============================================================
17. WHY PAGE ALIGNMENT IS REQUIRED
============================================================

Page table entries map pages, not arbitrary byte ranges.

On x86:

    4 KB page granularity

So mapping must be:

    virtual page -> physical page

Not:

    virtual byte -> physical byte

Therefore:

    input address may be unaligned
    internal mapping is aligned
    returned pointer preserves original offset

============================================================
18. GET_VM_AREA()
============================================================

Code:

    area = get_vm_area(size, VM_IOREMAP | (flags << 20));

Purpose:

    Reserve a virtual address range from vmalloc/ioremap space.

This does not map physical memory yet.

It only says:

    "Give me unused kernel virtual address range of this size."

Example:

    area->addr = 0xffffc20000010000
    area->size = 4096

The flag:

    VM_IOREMAP

marks this as ioremap space.

The expression:

    flags << 20

stores cache attribute information inside vm_struct flags,
so iounmap() later knows whether it must undo direct-map attribute changes.

============================================================
19. VMALLOC / IOREMAP SPACE
============================================================

Kernel virtual address space has regions like:

    direct map:
        maps normal physical RAM

    vmalloc area:
        dynamic kernel virtual mappings

    ioremap area:
        device/MMIO mappings, usually from vmalloc area

    fixmap:
        fixed virtual addresses for special early mappings

ioremap uses vmalloc-like virtual space.

============================================================
20. AREA->PHYS_ADDR
============================================================

Code:

    area->phys_addr = phys_addr;

This records the physical address behind this virtual mapping.

Later iounmap() uses it:

    ioremap_change_attr(p->phys_addr, p->size, 0);

to restore direct-map attributes.

============================================================
21. IOREMAP_PAGE_RANGE()
============================================================

Code:

    if (ioremap_page_range((unsigned long)addr,
                           (unsigned long)addr + size,
                           phys_addr,
                           pgprot)) {
        remove_vm_area(...);
        return NULL;
    }

Purpose:

    Actually create page table entries.

It maps:

    virtual addr             -> physical phys_addr
    virtual addr + PAGE_SIZE -> physical phys_addr + PAGE_SIZE
    ...

Using pgprot attributes.

Example:

    virtual 0xffffc20000010000 -> physical 0xfebf0000
    virtual 0xffffc20000011000 -> physical 0xfebf1000

If page table creation fails:

    remove reserved vm area
    return NULL

============================================================
22. CACHE ATTRIBUTE FIXUP
============================================================

Code:

    if (flags && ioremap_change_attr(phys_addr, size, flags) < 0) {
        area->flags &= 0xffffff;
        vunmap(addr);
        return NULL;
    }

Meaning:

    If special flags are requested, such as uncached,
    update direct-map attributes too.

Why?

Avoid this bad situation:

    direct map:
        physical address mapped cacheable

    ioremap:
        same physical address mapped uncached

This can confuse CPU cache behavior.

So both mappings should agree.

If changing attributes fails:

    clear stored high flag bits
    vunmap(addr)
    return NULL

============================================================
23. RETURN VALUE
============================================================

Code:

    return (__force void __iomem *)(offset + (char *)addr);

Remember:

    addr = page-aligned virtual mapping
    offset = original offset inside physical page

So return pointer corresponds exactly to original phys_addr.

Example:

    original phys = 0xfebf0123
    mapped addr   = 0xffffc20000010000
    offset        = 0x123

Return:

    0xffffc20000010123

============================================================
24. FULL __ioremap() FLOW DIAGRAM
============================================================

    __ioremap(phys_addr, size, flags)
        |
        v
    calculate last_addr
        |
        +-- size == 0? ---------------------> return NULL
        |
        +-- overflow? ----------------------> return NULL
        |
        v
    is low ISA area?
        |
        +-- yes ----------------------------> return phys_to_virt(phys)
        |
        v
    is normal RAM under high_memory?
        |
        +-- yes:
        |       check PageReserved for each page
        |           |
        |           +-- any not reserved ---> return NULL
        |
        v
    build pgprot flags
        |
        v
    save offset inside page
        |
        v
    align phys_addr down to page boundary
        |
        v
    align size up to page boundary
        |
        v
    get_vm_area(size, VM_IOREMAP)
        |
        +-- fail ---------------------------> return NULL
        |
        v
    area->phys_addr = aligned phys_addr
        |
        v
    ioremap_page_range(virt, virt+size, phys, pgprot)
        |
        +-- fail:
        |       remove_vm_area()
        |       return NULL
        |
        v
    flags != 0?
        |
        +-- yes:
        |       ioremap_change_attr()
        |           |
        |           +-- fail:
        |                   vunmap()
        |                   return NULL
        |
        v
    return addr + offset

============================================================
25. FUNCTION: ioremap_nocache()
============================================================

Code:

    void __iomem *ioremap_nocache(unsigned long phys_addr,
                                  unsigned long size)
    {
        return __ioremap(phys_addr, size, _PAGE_PCD);
    }

Meaning:

    Map physical device memory as uncached.

_PAGE_PCD:

    Page Cache Disable

Use this for:

    - device control registers
    - MMIO registers
    - places where caching would be wrong

Example:

    dev->mmio = ioremap_nocache(pci_resource_start(pdev, 0),
                                pci_resource_len(pdev, 0));

Then driver uses:

    writel(cmd, dev->mmio + COMMAND_REG);
    status = readl(dev->mmio + STATUS_REG);

============================================================
26. WHY UNCACHED?
============================================================

Device registers are not normal memory.

If CPU caches device register reads:

    status = readl(STATUS_REG)

could return stale value.

If CPU buffers/caches writes:

    writel(START, COMMAND_REG)

device may not see it immediately.

So MMIO register mappings are often uncached.

============================================================
27. FUNCTION: iounmap()
============================================================

Signature:

    void iounmap(volatile void __iomem *addr)

Purpose:

    Remove mapping created by ioremap.

It:

    - ignores direct-map/ISA mappings
    - finds vm_struct in vmlist
    - restores direct-map cache attributes if needed
    - removes vmalloc mapping
    - frees vm_struct

============================================================
28. iounmap(): IGNORE DIRECT MAP
============================================================

Code:

    if (addr <= high_memory)
        return;

Meaning:

    If address is inside normal direct-mapped RAM region,
    do nothing.

Because this was not created by ioremap vmalloc mapping.

============================================================
29. iounmap(): IGNORE ISA AREA
============================================================

Code:

    if (addr >= phys_to_virt(ISA_START_ADDRESS) &&
        addr < phys_to_virt(ISA_END_ADDRESS))
        return;

Because __ioremap() returned existing direct mapping for ISA area.

There is no vm_area to remove.

============================================================
30. iounmap(): PAGE ALIGN ADDRESS
============================================================

Code:

    addr = (volatile void __iomem *)(PAGE_MASK &
           (unsigned long __force)addr);

Why?

ioremap may have returned:

    addr + offset

But vm_area was created at page-aligned base.

Example:

    returned addr = 0xffffc20000010123

iounmap aligns:

    base addr = 0xffffc20000010000

Then it can find vm_struct.

============================================================
31. iounmap(): FIND VM_STRUCT
============================================================

Code:

    read_lock(&vmlist_lock);
    for (p = vmlist; p; p = p->next) {
        if (p->addr == addr)
            break;
    }
    read_unlock(&vmlist_lock);

The kernel keeps a global list of vmalloc/ioremap areas:

    vmlist

iounmap searches for the vm_struct whose addr matches.

If not found:

    printk("iounmap: bad address")
    dump_stack()
    return

This means caller passed an invalid pointer.

============================================================
32. iounmap(): RESTORE DIRECT MAPPING
============================================================

Code:

    if (p->flags >> 20)
        ioremap_change_attr(p->phys_addr, p->size, 0);

Remember __ioremap stored:

    VM_IOREMAP | (flags << 20)

So:

    p->flags >> 20

checks if special page attribute flags were saved.

If yes, restore direct mapping to normal attributes:

    flags = 0

So direct map becomes normal kernel page attributes again.

============================================================
33. iounmap(): REMOVE VM AREA
============================================================

Code:

    o = remove_vm_area((void *)addr);
    BUG_ON(p != o || o == NULL);
    kfree(p);

remove_vm_area:

    - removes virtual mapping
    - removes page table entries
    - removes from vmalloc list

Then kfree(p) frees vm_struct metadata.

============================================================
34. FULL IUNMAP FLOW DIAGRAM
============================================================

    iounmap(addr)
        |
        v
    addr <= high_memory?
        |
        +-- yes ----------------------> return
        |
        v
    addr in ISA direct mapping?
        |
        +-- yes ----------------------> return
        |
        v
    align addr down to page boundary
        |
        v
    search vmlist for vm_struct
        |
        +-- not found:
        |       printk bad address
        |       dump_stack
        |       return
        |
        v
    did mapping have special cache flags?
        |
        +-- yes:
        |       restore direct-map attributes
        |
        v
    remove_vm_area(addr)
        |
        v
    kfree(vm_struct)
        |
        v
    done

============================================================
35. COMPLETE MAP/UNMAP LIFETIME
============================================================

Driver probe:

    phys = pci_resource_start(pdev, 0);
    len  = pci_resource_len(pdev, 0);

    base = ioremap_nocache(phys, len);

Runtime:

    writel(value, base + reg);
    value = readl(base + reg);

Driver remove:

    iounmap(base);

Kernel internal lifetime:

    ioremap_nocache()
        |
        v
    __ioremap()
        |
        v
    get_vm_area()
        |
        v
    ioremap_page_range()
        |
        v
    return __iomem pointer

    iounmap()
        |
        v
    find vm_area
        |
        v
    remove_vm_area()
        |
        v
    free metadata

============================================================
36. PAGE TABLE VIEW
============================================================

Before ioremap:

    kernel virtual ioremap area:
        unused

After ioremap:

    virtual address                 physical address
    ------------------------------------------------
    0xffffc20000010000      --->    0x00000000febf0000
    0xffffc20000011000      --->    0x00000000febf1000

With flags:

    present
    writable
    global
    dirty
    accessed
    cache disabled if _PAGE_PCD

After iounmap:

    virtual address                 physical address
    ------------------------------------------------
    0xffffc20000010000      --->    unmapped
    0xffffc20000011000      --->    unmapped

============================================================
37. IMPORTANT BUG THIS CODE PREVENTS
============================================================

Bad idea:

    void *p = __va(device_phys_addr);
    *(u32 *)p = value;

Problems:

    - address may not be mapped
    - cache type may be wrong
    - compiler may optimize normal memory access
    - device ordering rules may be violated
    - no __iomem checking

Correct:

    void __iomem *base;

    base = ioremap_nocache(device_phys_addr, size);
    writel(value, base + offset);
    iounmap(base);

============================================================
38. DIRECT MAP VS IOREMAP
============================================================

Direct map:

    physical RAM
        |
        v
    fixed kernel virtual address via __va()

ioremap:

    arbitrary physical/device address
        |
        v
    dynamically allocated kernel virtual address

Diagram:

    Physical address space:

    +-----------------------------+
    | normal RAM                  |
    +-----------------------------+
    | reserved holes              |
    +-----------------------------+
    | PCI MMIO BAR                |
    +-----------------------------+
    | APIC / HPET / firmware      |
    +-----------------------------+

    Kernel virtual address space:

    +-----------------------------+
    | direct map of RAM           |
    +-----------------------------+
    | vmalloc/ioremap area        |
    |   PCI BAR mapping           |
    |   device register mapping   |
    +-----------------------------+
    | fixmap                      |
    +-----------------------------+

============================================================
39. WHY EXPORT_SYMBOL?
============================================================

Code:

    EXPORT_SYMBOL(__ioremap);
    EXPORT_SYMBOL(ioremap_nocache);
    EXPORT_SYMBOL(iounmap);

This allows loadable kernel modules to use these functions.

Example:

    my_driver.ko
        |
        +--> ioremap_nocache()
        +--> iounmap()

Without EXPORT_SYMBOL, modules could not link against them.

============================================================
40. SHORT SUMMARY
============================================================

__ioremap():

    creates a kernel virtual mapping for physical MMIO/device memory.

ioremap_nocache():

    wrapper around __ioremap() that disables CPU caching.

iounmap():

    destroys the mapping and restores direct-map cache attributes.

Main reason:

    device memory is not normal RAM and must be mapped/accessed with
    correct page table attributes and MMIO accessors.

