```text
============================================================
AMD64 GART IOMMU
File: arch/x86_64/kernel/aperture.c / pci-gart.c
Linux 2.6.x Era
============================================================

PURPOSE
------------------------------------------------------------

This file implements the AMD Hammer (K8) GART IOMMU.

Historically:

    AGP GART

was designed for graphics cards.

Linux reuses the same hardware as a general-purpose IOMMU.

Goal:

    Allow 32-bit PCI devices to DMA on systems
    containing more than 4GB RAM.

============================================================
HISTORICAL PROBLEM
============================================================

Year ~2002

Server:

    8GB RAM

Device:

    32-bit PCI card

Device DMA limit:

    0xffffffff

Maximum address:

    4GB

============================================================

RAM Layout

0GB ---------------- 4GB ---------------- 8GB

Device can DMA here:
^^^^^^^^^^^^^^^^^^^^

Cannot DMA here:
                     ^^^^^^^^^^^^^^^^^^^^

============================================================
WITHOUT GART
============================================================

Driver allocates:

    0x180000000

(6GB physical)

Device tries DMA:

    FAIL

because:

    6GB > 4GB

============================================================
WITH GART
============================================================

Device sees:

    0xF1000000

GART translates:

    0xF1000000
         |
         v
    0x180000000

Problem solved.

============================================================
BIG PICTURE
============================================================

PCI Device
      |
      | DMA Address
      v
+-------------------+
| AMD GART IOMMU    |
+-------------------+
      |
      | Translation
      v
Physical RAM

============================================================
IMPORTANT CONCEPT
============================================================

CPU:

Virtual Address
      |
      v
Page Table
      |
      v
Physical Address

------------------------------------------------------------

Device:

DMA Address
      |
      v
GART Entry
      |
      v
Physical Address

============================================================
KEY VARIABLES
============================================================

iommu_bus_base

Beginning of DMA aperture.

------------------------------------------------------------

iommu_size

Size of aperture.

------------------------------------------------------------

iommu_pages

Number of pages.

------------------------------------------------------------

iommu_gatt_base

GART Translation Table.

This is the page table for devices.

============================================================
WHAT IS GATT?
============================================================

GATT

Graphics Address Translation Table

Think:

    Device Page Table

============================================================
CPU SIDE
============================================================

PTE:

VA -> PA

============================================================
DEVICE SIDE
============================================================

GATT Entry:

DMA -> PA

============================================================
AGP APERTURE
============================================================

Hardware reserves a window.

Example:

Physical RAM:

0x180000000

============================================================

Device sees:

0xF0000000

============================================================

Hardware performs:

0xF0000000
      |
      v
0x180000000

============================================================
GPTE FORMAT
============================================================

GART Page Table Entry

Macros:

    GPTE_ENCODE()
    GPTE_DECODE()

============================================================

Entry contains:

    Physical page address

    VALID bit

    COHERENT bit

============================================================
VALID BIT
============================================================

GPTE_VALID

Translation exists.

============================================================
COHERENT BIT
============================================================

GPTE_COHERENT

Hardware cache coherency support.

============================================================
ALLOCATION SYSTEM
============================================================

GART aperture is treated like virtual memory.

============================================================

DMA Pages

0
1
2
3
4
5
6
7

============================================================

Bitmap

0 = free

1 = allocated

============================================================
ALLOCATOR
============================================================

alloc_iommu()

Purpose:

    Find free DMA pages.

============================================================
FLOW
============================================================

Lock bitmap
      |
      v
Find zero range
      |
      v
Mark allocated
      |
      v
Return offset

============================================================
BITMAP EXAMPLE
============================================================

0 0 1 1 0 0 0

Need 2 pages

Allocate:

1 1 1 1 0 0 0

============================================================
WRAPAROUND
============================================================

Allocator remembers:

    next_bit

============================================================

Search starts:

    next_bit -> end

============================================================

If no space:

    wrap to 0

============================================================

When wrap occurs:

    need_flush = 1

============================================================
WHY FLUSH?
============================================================

Device TLB equivalent exists inside GART.

Old translations may be cached.

Reusing an old mapping requires invalidation.

============================================================
flush_gart()
============================================================

Purpose:

    Invalidate hardware translation cache.

Equivalent to:

CPU:

    flush_tlb()

============================================================
FLOW
============================================================

need_flush?

   |
   +--> no

   |
   +--> yes
            |
            v
      k8_flush_garts()
            |
            v
      need_flush=0

============================================================
FULL FLUSH MODE
============================================================

iommu_fullflush = 1

============================================================

Every allocation:

    force flush

============================================================

Safer.

============================================================

Slower.

============================================================
LAZY FLUSH MODE
============================================================

iommu_fullflush = 0

============================================================

Flush only when:

    mappings reused

============================================================

Faster.

============================================================

Historically buggy on:

    3ware
    Qlogic

============================================================
MAPPING DECISION
============================================================

need_iommu()

============================================================

Inputs:

    DMA mask
    physical address
    size

============================================================

Question:

Can device reach memory directly?

============================================================

Example:

Device mask:

    0xffffffff

Memory:

    0x180000000

============================================================

Answer:

NO

Need IOMMU.

============================================================
DIRECT DMA CASE
============================================================

Physical:

0x20000000

Below 4GB

============================================================

Device supports 32-bit

============================================================

Return physical address directly.

============================================================
IOMMU CASE
============================================================

Physical:

0x180000000

Above 4GB

============================================================

Device supports only 32-bit

============================================================

Must use GART.

============================================================
dma_map_area()
============================================================

MOST IMPORTANT FUNCTION

============================================================

Input:

Physical memory

============================================================

Output:

DMA address

============================================================
FLOW
============================================================

Allocate GART pages
       |
       v
Create GPTE entries
       |
       v
Return DMA address

============================================================
EXAMPLE
============================================================

Physical:

0x180000000

============================================================

Allocated aperture slot:

Page 100

============================================================

GPTE[100]

    -> 0x180000000

============================================================

Device DMA address:

iommu_bus_base + 100*PAGE_SIZE

============================================================
ACTUAL TABLE BUILD
============================================================

for each page:

    iommu_gatt_base[i] =
            GPTE_ENCODE(physical)

Exactly like:

    CPU page table build

============================================================
dma_map_single()
============================================================

Driver:

    dma_map_single()

============================================================

Flow:

Physical address
      |
      v
need_iommu()
      |
      +--> no
      |      |
      |      +--> return physical
      |
      +--> yes
             |
             v
       dma_map_area()
             |
             v
       flush_gart()

============================================================
UNMAP FLOW
============================================================

gart_unmap_single()

============================================================

DMA address
      |
      v
Find aperture page
      |
      v
Replace entries with:

gart_unmapped_entry

      |
      v
Clear bitmap

============================================================
WHY REPLACE WITH
gart_unmapped_entry?
============================================================

Old hardware bug workaround.

============================================================

Bad entry = 0

could generate bus faults.

============================================================

Instead:

Unmapped entry points to:

    scratch page

============================================================

Unexpected DMA

        |

goes into harmless zero page

============================================================
SCRATCH PAGE
============================================================

Allocated during initialization.

============================================================

All unused GPTEs point here.

============================================================

Bad DMA:

    never hits random RAM

============================================================
EMERGENCY PAGES
============================================================

Reserved:

EMERGENCY_PAGES = 32

============================================================

128KB reserved at beginning.

============================================================

Used when aperture overflows.

============================================================
IOMMU OVERFLOW
============================================================

What if aperture full?

============================================================

No free GPTEs.

============================================================

alloc_iommu()

returns:

    -1

============================================================
FLOW
============================================================

overflow
     |
     v
iommu_full()
     |
     v
Log error

============================================================

Large DMA?

============================================================

panic()

============================================================
WHY PANIC?
============================================================

Without translation:

DMA may hit random RAM.

============================================================

Memory corruption worse than panic.

============================================================
SCATTER GATHER DMA
============================================================

Modern devices DMA many buffers.

============================================================

Buffer A

Buffer B

Buffer C

============================================================

Driver:

dma_map_sg()

============================================================

Implementation:

gart_map_sg()

============================================================
SPECIAL FEATURE:
MERGING
============================================================

Can merge adjacent SG entries.

============================================================

Page A
Page B
Page C

============================================================

Instead of:

3 DMA mappings

============================================================

Build:

1 contiguous DMA mapping

============================================================

Improves:

    block devices
    network devices

============================================================
dma_map_cont()
============================================================

Builds one large DMA region.

============================================================

SG entries
      |
      v
Continuous aperture pages
      |
      v
Single DMA range

============================================================
FLOW
============================================================

SG[0]
SG[1]
SG[2]

       |

Allocate aperture pages

       |

Create GPTEs

       |

Return one DMA region

============================================================
IOMMU LEAK DEBUGGING
============================================================

CONFIG_IOMMU_LEAK

============================================================

Tracks:

Who allocated GPTEs

============================================================

Stores:

__builtin_return_address()

============================================================

Useful for:

Driver forgets dma_unmap()

============================================================
GART INITIALIZATION
============================================================

Entry:

gart_iommu_init()

============================================================
STEP 1
============================================================

Find K8 northbridges

cache_k8_northbridges()

============================================================
STEP 2
============================================================

Locate AGP aperture

read_aperture()

============================================================
Reads:

PCI config

0x90
0x94

============================================================
STEP 3
============================================================

Determine aperture size

Example:

128MB

256MB

512MB

============================================================
STEP 4
============================================================

Reserve IOMMU portion

Example:

AGP aperture

256MB

============================================================

AGP uses:

128MB

============================================================

IOMMU uses:

128MB

============================================================
STEP 5
============================================================

Allocate bitmap

============================================================
STEP 6
============================================================

Reserve emergency pages

============================================================
STEP 7
============================================================

Initialize all GPTEs

to scratch page

============================================================
STEP 8
============================================================

Flush hardware

============================================================
STEP 9
============================================================

Install backend

dma_ops = &gart_dma_ops

============================================================
DMA OPS TABLE
============================================================

map_single

    gart_map_single

------------------------------------------------------------

unmap_single

    gart_unmap_single

------------------------------------------------------------

map_sg

    gart_map_sg

------------------------------------------------------------

unmap_sg

    gart_unmap_sg

============================================================
BOOT DECISION TREE
============================================================

Boot
 |
 v
gart_iommu_init()
 |
 +--> AMD K8 present?
 |       |
 |       +--> no
 |              return
 |
 +--> Aperture available?
 |       |
 |       +--> no
 |              return
 |
 +--> Memory > 4GB ?
 |       |
 |       +--> yes
 |
 +--> Setup GATT
 |
 +--> Setup Bitmap
 |
 +--> Setup Scratch Page
 |
 +--> Flush GART
 |
 +--> dma_ops = gart_dma_ops

============================================================
COMPLETE DMA FLOW
============================================================

Driver
 |
 v
dma_map_single()
 |
 v
gart_map_single()
 |
 v
need_iommu()?
 |
 +--> no
 |       |
 |       +--> return physical address
 |
 +--> yes
         |
         v
    alloc_iommu()
         |
         v
    Build GPTE entries
         |
         v
    flush_gart()
         |
         v
    Return DMA address

============================================================
MENTAL MODEL
============================================================

Think of this file as implementing:

CPU MMU
--------------------------------

Virtual Address
      |
      v
Page Table
      |
      v
Physical Memory

============================================================

AMD GART IOMMU
--------------------------------

DMA Address
      |
      v
GATT Table
      |
      v
Physical Memory

============================================================

The allocator behaves like:

    vmalloc allocator

The GATT behaves like:

    page tables

flush_gart() behaves like:

    flush_tlb()

============================================================
WHY THIS FILE IS IMPORTANT
============================================================

This is one of the first practical Linux IOMMU
implementations.

Almost every modern IOMMU concept already exists here:

    DMA remapping
    DMA aperture
    IOVA allocation
    IOTLB flushing
    SG merging
    DMA fault handling
    device isolation

Intel VT-d and AMD-Vi are more sophisticated,
but architecturally they solve the same problem.

============================================================
ONE-LINE SUMMARY
============================================================

The AMD64 GART IOMMU driver repurposes the K8 AGP aperture
as a device MMU, allocating DMA virtual addresses from a
bitmap-managed aperture, creating GATT translation entries
that map DMA addresses to physical memory, flushing GART
translation caches when needed, and allowing 32-bit PCI
devices to DMA safely on systems with more than 4GB RAM.
============================================================
```

This source is one of the best bridges between "normal MMU thinking" and "modern IOMMU thinking": the GATT is essentially a page table, `alloc_iommu()` is a virtual-address allocator, and `flush_gart()` is the device-side equivalent of a TLB flush. 

