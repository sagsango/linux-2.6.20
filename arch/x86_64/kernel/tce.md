```text
===============================================================================
FILE: arch/x86_64/kernel/tce.c
PURPOSE: IBM CALGARY IOMMU TCE TABLE MANAGEMENT
KERNEL: Linux 2.6.x
===============================================================================

BACKGROUND
===============================================================================

This file manages translation entries for the IBM Calgary IOMMU.

Calgary is an old IBM x86-64 server IOMMU.

Its job is to translate device DMA addresses into physical memory addresses.

Modern equivalents:

    Intel VT-d

    AMD-Vi

    ARM SMMU

But Calgary uses older terminology:

    TCE = Translation Control Entry

Think:

    TCE == IOMMU page table entry

===============================================================================
WHY AN IOMMU NEEDS A TABLE
===============================================================================

Without IOMMU:

PCI device DMA address
        |
        v
physical memory directly

Example:

    device DMA -> 0x12345000

The device directly accesses physical RAM.

===============================================================================

With Calgary IOMMU:

PCI device DMA address
        |
        v
Calgary IOMMU
        |
        v
TCE table lookup
        |
        v
physical memory

Example:

    device DMA address 0x00001000
        |
        v
    TCE[1]
        |
        v
    physical page 0x12345000

===============================================================================
CPU MMU vs CALGARY IOMMU
===============================================================================

CPU:

Virtual address
        |
        v
CPU page table
        |
        v
physical address

-------------------------------------------------------------------------------

Calgary:

DMA address
        |
        v
TCE table
        |
        v
physical address

===============================================================================
MAIN RESPONSIBILITY OF THIS FILE
===============================================================================

This file does not implement all Calgary hardware control.

It specifically manages the TCE table:

    allocate table

    free table

    initialize iommu_table metadata

    allocate bitmap

    build TCE entries

    clear TCE entries

    flush TCE entries to memory

Other Calgary code uses this file when mapping/unmapping DMA.

===============================================================================
IMPORTANT STRUCTURES
===============================================================================

struct iommu_table

Conceptually:

    one DMA page table for one PCI bus / Calgary PHB

Important fields used here:

    it_base

        base address of TCE table

    it_size

        number of TCE entries

    it_map

        bitmap tracking allocated entries

    it_hint

        allocation hint

    it_lock

        protects table allocation

    it_busno

        PCI bus number

    bbar

        Calgary bridge MMIO register base

===============================================================================
WHAT IS A TCE?
===============================================================================

TCE = Translation Control Entry.

Each TCE describes one DMA page.

Contains:

    real page number

    read permission

    write permission

    valid/permission bits

Conceptually:

    TCE[index] = physical_page | permissions

===============================================================================
DMA DIRECTION AND PERMISSIONS
===============================================================================

Linux DMA directions:

    DMA_TO_DEVICE

        device reads memory

        example: disk write, network transmit

    DMA_FROM_DEVICE

        device writes memory

        example: disk read, network receive

    DMA_BIDIRECTIONAL

        device may read and write

This file programs TCE permissions from direction.

===============================================================================
tce_build()
===============================================================================

Most important function.

Prototype:

    void tce_build(struct iommu_table *tbl,
                   unsigned long index,
                   unsigned int npages,
                   unsigned long uaddr,
                   int direction)

Purpose:

    create TCE entries mapping DMA pages to physical pages.

Input:

    tbl

        table to modify

    index

        starting TCE index

    npages

        number of pages to map

    uaddr

        kernel virtual address of buffer

    direction

        DMA direction

===============================================================================
tce_build() FLOW
===============================================================================

1. Build permission bits.

2. Locate first TCE entry:

       tp = tbl->it_base + index

3. For each page:

       physical bus page = virt_to_bus(uaddr) >> PAGE_SHIFT

       insert page number into TCE

       store TCE in big-endian format

       flush TCE from CPU cache

       move to next page

===============================================================================
PERMISSION SETUP
===============================================================================

Code starts with:

    t = READ permission

Then:

    if direction != DMA_TO_DEVICE:
        add WRITE permission

Meaning:

DMA_TO_DEVICE:

    device only needs to read system memory

DMA_FROM_DEVICE:

    device needs to write system memory

DMA_BIDIRECTIONAL:

    device needs write permission too

===============================================================================
WHY DMA_TO_DEVICE ONLY NEEDS READ
===============================================================================

Example:

    network transmit

CPU prepared packet in RAM.

Device reads packet from RAM.

Device does not need to write that RAM.

So TCE grants:

    device read

not:

    device write

===============================================================================
WHY DMA_FROM_DEVICE NEEDS WRITE
===============================================================================

Example:

    disk read

Device writes data into RAM buffer.

So TCE must grant:

    device write

===============================================================================
TCE BUILD EXAMPLE
===============================================================================

Input:

    index  = 100
    npages = 2
    uaddr  = 0xffff810012345000

Assume:

    virt_to_bus(uaddr) = 0x12345000

Then:

    TCE[100] -> physical page 0x12345
    TCE[101] -> physical page 0x12346

Device sees DMA address based on index.

Calgary translates through TCE table.

===============================================================================
WHY cpu_to_be64()
===============================================================================

Code:

    *tp = cpu_to_be64(t)

The Calgary hardware expects TCE entries in big-endian format.

Even though x86 is little-endian, table entries are written as big-endian.

So software must byte-swap on x86.

===============================================================================
flush_tce()
===============================================================================

Purpose:

    make sure hardware sees updated TCE entry.

Code:

    if cpu_has_clflush:
        clflush(tceaddr)
    else:
        wbinvd

===============================================================================
WHY FLUSH TCE?
===============================================================================

CPU writes TCE entry into memory.

But it may remain only in CPU cache.

Calgary hardware reads TCE table from memory.

If hardware sees stale memory, DMA translation fails.

Therefore:

    flush cache line containing TCE

===============================================================================
clflush vs wbinvd
===============================================================================

clflush:

    flush one cache line

    precise and cheaper

wbinvd:

    write back and invalidate entire cache

    very expensive

Fallback if CPU lacks CLFLUSH.

===============================================================================
tce_free()
===============================================================================

Purpose:

    clear TCE entries during DMA unmap.

Flow:

    locate TCE[index]

    for each page:
        write 0
        flush TCE cache line

Result:

    device DMA through that entry is no longer valid

===============================================================================
WHY CLEAR TCEs?
===============================================================================

When DMA mapping ends:

    device should no longer access that memory.

Clearing TCE prevents stale DMA access.

This is IOMMU protection.

===============================================================================
table_size_to_number_of_entries()
===============================================================================

Purpose:

    convert Calgary table size encoding into entry count.

Comment:

    size is order 0-7

    smallest table is 8K entries

Code:

    return (1 << size) << 13

Examples:

    size = 0 -> 8192 entries

    size = 1 -> 16384 entries

    size = 2 -> 32768 entries

===============================================================================
TCE TABLE SIZE
===============================================================================

Each entry maps one page.

If page size is 4KB:

    8192 entries maps 32MB DMA space

    16384 entries maps 64MB

    32768 entries maps 128MB

===============================================================================
tce_table_setparms()
===============================================================================

Purpose:

    initialize software metadata for one IOMMU table.

Flow:

    set bus number

    compute table size

    allocate bitmap

    clear bitmap

    set allocation hint

    initialize spinlock

===============================================================================
WHY BITMAP?
===============================================================================

TCE table entries are a finite resource.

Need to know which entries are free.

Bitmap:

    0 = free

    1 = allocated

Example:

    0 0 1 1 0 0 0

DMA map request needs 2 pages:

    allocate first two zero bits

===============================================================================
BITMAP SIZE
===============================================================================

Need one bit per TCE entry.

Code:

    bitmapsz = tbl->it_size / BITS_PER_BYTE

Example:

    8192 entries

needs:

    8192 / 8 = 1024 bytes bitmap

===============================================================================
it_hint
===============================================================================

Allocation hint.

Starts at:

    0

Used by allocator elsewhere to avoid scanning from beginning every time.

This improves performance.

===============================================================================
it_lock
===============================================================================

Spinlock protecting TCE table allocation.

Needed because multiple CPUs/drivers may map DMA simultaneously.

===============================================================================
build_tce_table()
===============================================================================

Purpose:

    allocate and initialize struct iommu_table for a PCI device/bus.

Flow:

    check dev->sysdata is empty

    kzalloc iommu_table

    tce_table_setparms()

    store BBAR

    attach table to dev->sysdata

===============================================================================
WHY dev->sysdata?
===============================================================================

The PCI device needs a place to store architecture-specific data.

Here:

    dev->sysdata = tbl

So later DMA code can find the Calgary IOMMU table for that PCI bus/device.

Comment says:

    NUMA is already using bus->sysdata

So Calgary uses the PCI device's sysdata instead.

===============================================================================
BBAR
===============================================================================

BBAR = Base Bridge Address Register.

It points to Calgary bridge MMIO register area.

Stored in:

    tbl->bbar

Other Calgary code uses it to program hardware.

===============================================================================
alloc_tce_table()
===============================================================================

Purpose:

    allocate actual hardware TCE table memory.

Flow:

    compute number of entries

    multiply by TCE_ENTRY_SIZE

    allocate low boot memory

Code:

    __alloc_bootmem_low(size, size, 0)

===============================================================================
WHY BOOTMEM?
===============================================================================

TCE table must be allocated early.

The normal page allocator may not be fully available yet.

Also hardware may require the table to be in low physical memory.

So bootmem allocator is used.

===============================================================================
WHY ALIGNED TO SIZE?
===============================================================================

__alloc_bootmem_low(size, size, 0)

The second argument is alignment.

So table is aligned to its own size.

Hardware often requires naturally aligned translation tables.

===============================================================================
free_tce_table()
===============================================================================

Purpose:

    free previously allocated TCE table.

Flow:

    if tbl is NULL:
        return

    compute size

    free_bootmem(__pa(tbl), size)

===============================================================================
COMPLETE DMA MAP FLOW WITH THIS FILE
===============================================================================

Driver
    |
    v
dma_map_single()
    |
    v
Calgary DMA backend
    |
    v
allocate TCE indices from bitmap
    |
    v
tce_build()
    |
    v
write TCE entries
    |
    v
flush TCE cache lines
    |
    v
return DMA address to device

===============================================================================
COMPLETE DMA UNMAP FLOW
===============================================================================

Driver
    |
    v
dma_unmap_single()
    |
    v
Calgary DMA backend
    |
    v
tce_free()
    |
    v
clear TCE entries
    |
    v
flush TCE cache lines
    |
    v
free bitmap entries

===============================================================================
COMPLETE INITIALIZATION FLOW
===============================================================================

Calgary detected
    |
    v
alloc_tce_table()
    |
    v
build_tce_table()
    |
    v
tce_table_setparms()
    |
    v
bitmap allocated
    |
    v
table attached to PCI device
    |
    v
hardware programmed elsewhere with table address

===============================================================================
RELATION TO pci-calgary.c
===============================================================================

pci-calgary.c:

    detects Calgary hardware

    programs registers

    handles DMA map/unmap policy

    enables translation

-------------------------------------------------------------------------------

tce.c:

    manages table memory

    writes TCE entries

    clears TCE entries

    allocates bitmap metadata

===============================================================================
RELATION TO pci-dma.c
===============================================================================

pci-dma.c:

    generic x86-64 DMA dispatcher

Calgary installs dma_ops.

Those dma_ops eventually use:

    tce_build()

    tce_free()

to manage actual translations.

===============================================================================
RELATION TO SWIOTLB
===============================================================================

SWIOTLB:

    bounce buffer copy

Calgary:

    hardware translation table

With Calgary:

    no bounce copy needed

Device DMA address is translated by hardware.

===============================================================================
MENTAL MODEL
===============================================================================

Think of this file as:

    page table management for devices

CPU page table:

    PTE[index] = physical page + permissions

Calgary TCE table:

    TCE[index] = physical page + DMA permissions

Everything else is allocation, flushing, and metadata.

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

This file manages IBM Calgary IOMMU TCE tables by allocating table and
bitmap metadata, building big-endian TCE entries that map device DMA
pages to physical memory pages with direction-based permissions, flushing
those entries so hardware sees them, and clearing them on DMA unmap so
old device mappings cannot remain valid.
===============================================================================
```

