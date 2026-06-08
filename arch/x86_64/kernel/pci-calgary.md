============================================================
IBM CALGARY IOMMU
File: arch/x86_64/kernel/pci-calgary.c
Linux 2.6 Era
============================================================

PURPOSE
------------------------------------------------------------

This file implements support for the IBM Calgary IOMMU.

Calgary was one of the earliest hardware DMA-remapping engines
used on x86 servers before Intel VT-d became common.

This file provides:

    - DMA address translation
    - DMA protection
    - DMA allocation
    - Scatter/Gather mapping
    - TCE table management
    - Hardware initialization
    - DMA fault detection

Modern equivalent:

    Calgary     -> Intel VT-d
    Calgary     -> AMD-Vi (AMD IOMMU)

============================================================
THE PROBLEM CALGARY SOLVES
============================================================

Without IOMMU:

PCI Device
    |
    | DMA
    v
Physical Memory

Example:

NIC DMA ---> 0x12345000

Device directly accesses physical memory.

============================================================
WHY THIS IS BAD
============================================================

Buggy driver:

    Wrong DMA address

Buggy firmware:

    Wrong DMA address

Broken hardware:

    Wrong DMA address

Malicious device:

    Wrong DMA address

Result:

    Corrupt kernel memory
    Corrupt page tables
    Corrupt another process
    Corrupt filesystem buffers

No protection.

============================================================
WITH AN IOMMU
============================================================

PCI Device
      |
      | DMA Address (IOVA)
      v
+----------------+
| Calgary IOMMU  |
+----------------+
      |
      | Translation
      v
Physical RAM

Now devices cannot DMA anywhere.

They can only DMA where the IOMMU permits.

============================================================
IMPORTANT ANALOGY
============================================================

CPU uses:

Virtual Address
      |
      v
Page Table
      |
      v
Physical Address

------------------------------------------------------------

Device uses:

DMA Address
      |
      v
TCE Table
      |
      v
Physical Address

Same concept.

============================================================
WHAT IS A TCE?
============================================================

TCE

    Translation Control Entry

Modern name:

    IOMMU Page Table Entry

Think:

CPU:
    PTE

Calgary:
    TCE

============================================================
CPU EXAMPLE
============================================================

Virtual Page:

0x400000

Page Table Entry:

0x12345000

Result:

0x400000 -> 0x12345000

============================================================
DEVICE EXAMPLE
============================================================

DMA Page:

0x1000

TCE Entry:

0x34567000

Result:

0x1000 -> 0x34567000

============================================================
HIGH LEVEL ARCHITECTURE
============================================================

Device
  |
  v
DMA Request
  |
  v
Calgary Hardware
  |
  v
TAR Register
  |
  v
TCE Table
  |
  v
Physical RAM

============================================================
KEY HARDWARE COMPONENTS
============================================================

PHB

    PCI Host Bridge

------------------------------------------------------------

TAR

    Table Address Register

------------------------------------------------------------

TCE Table

    DMA Page Table

------------------------------------------------------------

CSR

    DMA Error Status Register

------------------------------------------------------------

AER

    Arbitration Control Register

============================================================
MULTIPLE PHBs
============================================================

System

+--------------------+
| Calgary Chip       |
|                    |
| PHB0               |
| PHB1               |
| PHB2               |
| PHB3               |
+--------------------+

Each PHB controls a PCI bus.

============================================================
IMPORTANT DATA STRUCTURES
============================================================

struct iommu_table

This is the heart of the entire file.

Think:

    CPU mm_struct

for devices.

Contains:

    TCE table

    bitmap allocator

    lock

    table size

    bus information

============================================================
BOOT FLOW
============================================================

Kernel Boot
     |
     v
detect_calgary()
     |
     v
Locate Calgary Hardware
     |
     v
Allocate TCE Tables
     |
     v
calgary_iommu_init()
     |
     v
calgary_init()
     |
     v
Enable Translation
     |
     v
Install DMA Operations

============================================================
PHASE 1:
DETECT HARDWARE
============================================================

Function:

    detect_calgary()

============================================================
MODERN SYSTEM
============================================================

VT-d:

    ACPI DMAR Table

============================================================
CALGARY
============================================================

Uses:

    BIOS EBDA

EBDA:

Extended BIOS Data Area

============================================================
FLOW
============================================================

detect_calgary()
       |
       v
Locate EBDA
       |
       v
Locate Rio Grande Table
       |
       v
Parse Calgary Information
       |
       v
Allocate TCE Tables
       |
       v
Mark IOMMU Present

============================================================
WHY RIO GRANDE TABLE?
============================================================

IBM firmware stores Calgary topology inside:

    Rio Grande table

Located in:

    EBDA

The code walks BIOS structures manually.

Very old-school firmware discovery.

============================================================
PHASE 2:
LOCATE HARDWARE REGISTERS
============================================================

Function:

    calgary_locate_bbars()

============================================================
WHAT IS BBAR?
============================================================

BBAR

Base Bridge Address Register

Think:

    MMIO base address

for Calgary configuration space.

============================================================
FLOW
============================================================

Find Calgary Device
        |
        v
ioremap()
        |
        v
Map 1MB Register Space
        |
        v
Discover PHBs
        |
        v
Store MMIO Addresses

============================================================
PHASE 3:
BUILD TCE TABLE
============================================================

Function:

    calgary_setup_tar()

============================================================
FLOW
============================================================

Allocate TCE Table
       |
       v
Initialize Bitmap
       |
       v
Reserve Special Regions
       |
       v
Program TAR Register

============================================================
TAR REGISTER
============================================================

TAR

Table Address Register

Contains:

    Physical address of TCE table

============================================================
WITHOUT TAR
============================================================

Device DMA
      |
      v
Calgary

Does not know where page table lives.

============================================================
WITH TAR
============================================================

Device DMA
      |
      v
Calgary
      |
      v
TAR
      |
      v
TCE Table

============================================================
SPECIAL MEMORY RESERVATION
============================================================

Function:

    calgary_reserve_regions()

============================================================
WHY?
============================================================

Certain addresses are unsafe.

Examples:

    VGA hole
    BIOS region
    Peripheral windows

These must never be handed out as DMA addresses.

============================================================
RESERVED REGIONS
============================================================

bad_dma_address

------------------------------------------------------------

640KB - 1MB

Old VGA / BIOS hole

------------------------------------------------------------

Peripheral Memory Windows

MEM1

MEM2

============================================================
BITMAP ALLOCATOR
============================================================

TCE entries allocated using bitmap.

0

    Free

1

    Used

============================================================
EXAMPLE
============================================================

Bitmap:

0 0 1 1 0 0 0

Need 2 pages:

^ ^

Allocate:

1 1 1 1 0 0 0

============================================================
ALLOCATOR FUNCTIONS
============================================================

iommu_range_alloc()

    Allocate DMA pages

------------------------------------------------------------

iommu_range_reserve()

    Reserve DMA pages

------------------------------------------------------------

iommu_free()

    Release DMA pages

============================================================
DMA MAP SINGLE FLOW
============================================================

Driver
   |
   v
dma_map_single()
   |
   v
calgary_map_single()
   |
   v
iommu_alloc()
   |
   v
iommu_range_alloc()
   |
   v
tce_build()
   |
   v
Return DMA Address

============================================================
EXAMPLE
============================================================

Kernel Buffer:

0x34567000

============================================================

Driver:

dma_map_single()

============================================================

Calgary allocates:

DMA Page 0x1000

============================================================

Creates:

0x1000 ---> 0x34567000

============================================================

Device sees:

0x1000

NOT

0x34567000

============================================================
iommu_alloc()
============================================================

This is basically:

    alloc_pages()

for DMA space.

============================================================
FLOW
============================================================

Lock Table
      |
      v
Find Free Range
      |
      v
Build TCE Entries
      |
      v
Return DMA Address

============================================================
tce_build()
============================================================

This is the most important hardware operation.

============================================================
BEFORE
============================================================

DMA Page:

0x1000

No Translation

============================================================
AFTER
============================================================

TCE[0x1000]
      |
      v
Physical Page 0x34567000

============================================================
DMA UNMAP FLOW
============================================================

Driver
    |
    v
dma_unmap_single()
    |
    v
calgary_unmap_single()
    |
    v
iommu_free()
    |
    v
tce_free()

============================================================
RESULT
============================================================

Translation removed.

Bitmap entries released.

============================================================
SCATTER GATHER DMA
============================================================

Modern devices often DMA many pages.

Example:

Page A
Page B
Page C

============================================================
Driver
============================================================

dma_map_sg()

============================================================
Calgary
============================================================

calgary_map_sg()

============================================================
FLOW
============================================================

SG Entry 0
       |
       v
Allocate TCEs

SG Entry 1
       |
       v
Allocate TCEs

SG Entry 2
       |
       v
Allocate TCEs

============================================================

Build Hardware Translations

============================================================

Return DMA Addresses

============================================================
COHERENT DMA
============================================================

Function:

    calgary_alloc_coherent()

============================================================
FLOW
============================================================

Allocate Pages
       |
       v
Zero Memory
       |
       v
Create TCEs
       |
       v
Return:

    CPU Virtual Address

and

    DMA Address

============================================================
RESULT
============================================================

CPU and Device can share memory.

============================================================
ENABLE TRANSLATION
============================================================

Function:

    calgary_enable_translation()

============================================================
FLOW
============================================================

Program TAR
      |
      v
Enable TCE Bit
      |
      v
Enable Error Reporting
      |
      v
Start Watchdog

============================================================
BEFORE
============================================================

Device DMA
     |
     v
Physical Memory

============================================================
AFTER
============================================================

Device DMA
      |
      v
Calgary Translation
      |
      v
Physical Memory

============================================================
DMA FAULT DETECTION
============================================================

Function:

    calgary_watchdog()

============================================================
PURPOSE
============================================================

Detect:

    DMA faults

    Translation failures

    Bus errors

============================================================
FLOW
============================================================

Read CSR Register
       |
       v
DMA Error Present?
       |
   +---+---+
   |       |
   No      Yes
   |        |
   v        v
Reset   Log Error
Timer       |
             v
      Disable Offending Bus

============================================================
WHY DISABLE BUS?
============================================================

Bad DMA can destroy memory.

Better to disable device than corrupt system RAM.

============================================================
TCE CACHE
============================================================

Hardware caches TCE entries.

Similar to:

CPU TLB

============================================================
Device Side
============================================================

DMA Address
      |
      v
TCE Cache
      |
      v
TCE Table

============================================================
FUNCTION
============================================================

tce_cache_blast()

============================================================
PURPOSE
============================================================

Invalidate TCE Cache

Equivalent to:

CPU:

    flush_tlb()

============================================================
FLOW
============================================================

Pause DMA
      |
      v
Wait Split Queues Empty
      |
      v
Invalidate Cache
      |
      v
Resume DMA

============================================================
MODERN VT-d EQUIVALENTS
============================================================

Calgary                  VT-d

------------------------------------------------------------

TCE                      PTE

------------------------------------------------------------

TAR                      Root Pointer

------------------------------------------------------------

TCE Cache                IOTLB

------------------------------------------------------------

tce_cache_blast()        IOTLB Flush

------------------------------------------------------------

PHB                       Root Complex

------------------------------------------------------------

CSR Fault                 VT-d Fault

============================================================
COMPLETE DMA MAP FLOW
============================================================

Driver
 |
 v
dma_map_single()
 |
 v
calgary_map_single()
 |
 v
iommu_alloc()
 |
 v
iommu_range_alloc()
 |
 v
Allocate Bitmap Entries
 |
 v
tce_build()
 |
 v
Create TCE Entries
 |
 v
DMA Address Returned
 |
 v
Device DMA
 |
 v
Calgary
 |
 v
TCE Lookup
 |
 v
Physical Memory

============================================================
COMPLETE BOOT FLOW
============================================================

Kernel Boot
 |
 v
detect_calgary()
 |
 v
Locate Rio Grande Table
 |
 v
Locate Calgary Chips
 |
 v
Allocate TCE Tables
 |
 v
calgary_iommu_init()
 |
 v
calgary_init()
 |
 v
Locate BBAR Registers
 |
 v
Program TAR Registers
 |
 v
Enable Translation
 |
 v
Install dma_ops
 |
 v
Device Drivers Use IOMMU

============================================================
WHY THIS FILE IS IMPORTANT
============================================================

This file teaches almost every modern IOMMU concept:

    DMA remapping
    IOVA allocation
    DMA protection
    Scatter/Gather DMA
    IOMMU page tables
    IOTLB flushing
    DMA fault handling

The hardware is old.

The concepts are still used today in:

    Intel VT-d
    AMD-Vi
    ARM SMMU
    PCIe ATS
    SR-IOV
    Device Passthrough

============================================================
MENTAL MODEL
============================================================

Think of Calgary as:

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

Device MMU (Calgary)
--------------------------------

DMA Address
      |
      v
TCE Table
      |
      v
Physical Memory

============================================================

Everything in this file is simply implementing:

    "Page tables for PCI devices"

instead of

    "Page tables for CPUs"

============================================================
ONE-LINE SUMMARY
============================================================

The Calgary IOMMU file implements a complete DMA memory
management subsystem that allocates device-visible addresses,
creates TCE page-table entries, protects physical memory from
errant DMA, handles scatter-gather mappings, detects DMA
faults, and programs IBM Calgary hardware to perform DMA
address translation exactly like a CPU MMU translates virtual
addresses.
============================================================
