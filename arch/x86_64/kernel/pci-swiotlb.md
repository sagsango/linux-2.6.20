```text
============================================================
x86-64 SWIOTLB DMA GLUE
File: arch/x86_64/kernel/pci-swiotlb.c
Linux 2.6.x Era
============================================================

PURPOSE
------------------------------------------------------------

This file connects x86-64 DMA code to the generic SWIOTLB
implementation in:

    lib/swiotlb.c

SWIOTLB means:

    Software I/O Translation Lookaside Buffer

In simple words:

    software bounce buffering for DMA

============================================================
WHY SWIOTLB EXISTS
============================================================

Problem:

    Device can only DMA below 4GB

but kernel buffer is above 4GB.

Example:

RAM:

0GB ---------------- 4GB ---------------- 8GB

Device can DMA:
^^^^^^^^^^^^^^^

Buffer allocated:
                     ^^^^^

Device cannot reach it.

============================================================
WITHOUT SWIOTLB
============================================================

Device tries DMA to high address:

    0x180000000

But device only supports:

    32-bit DMA

Result:

    failure or corruption

============================================================
WITH SWIOTLB
============================================================

SWIOTLB allocates low-memory bounce buffers.

Flow:

High memory buffer
      |
      | CPU copy
      v
Low memory bounce buffer
      |
      | DMA
      v
Device

============================================================
BIG PICTURE
============================================================

Driver
   |
   v
dma_map_single()
   |
   v
swiotlb_map_single()
   |
   v
Maybe allocate bounce buffer
   |
   v
Return device-reachable DMA address

============================================================
WHAT THIS FILE DOES
============================================================

This file is very small.

It does only two things:

    1. Defines swiotlb_dma_ops

    2. Initializes SWIOTLB if needed

Actual SWIOTLB logic is in:

    lib/swiotlb.c

============================================================
GLOBAL FLAG
============================================================

int swiotlb;

Meaning:

    0 = not using SWIOTLB

    1 = using SWIOTLB

Exported so other kernel code can check it.

============================================================
DMA OPS TABLE
============================================================

struct dma_mapping_ops swiotlb_dma_ops

Provides backend methods:

    mapping_error
    alloc_coherent
    free_coherent
    map_single
    unmap_single
    sync_single_for_cpu
    sync_single_for_device
    sync_single_range_for_cpu
    sync_single_range_for_device
    sync_sg_for_cpu
    sync_sg_for_device
    map_sg
    unmap_sg

============================================================
WHY SO MANY SYNC FUNCTIONS?
============================================================

Because SWIOTLB may use bounce buffers.

CPU buffer and device buffer may be different memory.

So data may need copying.

============================================================
DMA TO DEVICE
============================================================

Example:

    disk write

CPU buffer:
    high memory

Bounce buffer:
    low memory

Flow:

CPU buffer
   |
   | copy before DMA
   v
bounce buffer
   |
   | device reads
   v
device

============================================================
DMA FROM DEVICE
============================================================

Example:

    disk read

Device writes:
    bounce buffer

After DMA completes:

bounce buffer
   |
   | copy after DMA
   v
CPU buffer

============================================================
WHY sync_single_for_cpu()
============================================================

Before CPU reads data after device DMA:

    copy bounce buffer back to real buffer

============================================================
WHY sync_single_for_device()
============================================================

Before device reads data:

    copy real buffer to bounce buffer

============================================================
pci_swiotlb_init()
============================================================

Main function in this file.

Flow:

    if no hardware IOMMU
    and no iommu=off
    and RAM > 4GB:
        enable SWIOTLB

    if swiotlb_force:
        enable SWIOTLB

    if enabled:
        swiotlb_init()
        dma_ops = &swiotlb_dma_ops

============================================================
CONDITION 1
============================================================

!iommu_detected

Meaning:

    no hardware IOMMU was found

============================================================
CONDITION 2
============================================================

!no_iommu

Meaning:

    user did not pass iommu=off

============================================================
CONDITION 3
============================================================

end_pfn > MAX_DMA32_PFN

Meaning:

    RAM exists above 4GB

============================================================
WHY RAM ABOVE 4GB MATTERS
============================================================

If all RAM is below 4GB:

    32-bit devices can address it directly

SWIOTLB often unnecessary.

If RAM above 4GB:

    32-bit devices may need bounce buffers.

============================================================
swiotlb_force
============================================================

If set:

    force SWIOTLB even if not strictly required.

Useful for:

    testing
    debugging
    platforms that require bounce buffering

============================================================
INITIALIZATION FLOW
============================================================

pci_swiotlb_init()
      |
      v
Decide whether SWIOTLB needed
      |
      v
swiotlb_init()
      |
      v
Allocate bounce buffer pool
      |
      v
dma_ops = swiotlb_dma_ops

============================================================
BOOT FLOW CONTEXT
============================================================

From pci-dma.c:

pci_iommu_alloc()
      |
      +--> detect Calgary
      |
      +--> pci_swiotlb_init()

Then later:

pci_iommu_init()
      |
      +--> calgary_iommu_init()
      +--> gart_iommu_init()
      +--> no_iommu_init()

============================================================
BACKEND PRIORITY
============================================================

Hardware IOMMU available?

    use hardware IOMMU

No hardware IOMMU and RAM > 4GB?

    use SWIOTLB

No SWIOTLB?

    use no-IOMMU direct mapping

============================================================
FULL DMA MAP FLOW WITH SWIOTLB
============================================================

Driver
 |
 v
dma_map_single(dev, buffer, size, direction)
 |
 v
swiotlb_map_single()
 |
 v
Can device address buffer directly?
 |
 +--> yes
 |       |
 |       v
 |   return direct DMA address
 |
 +--> no
         |
         v
    allocate bounce slot
         |
         v
    if DMA_TO_DEVICE:
         copy original buffer to bounce buffer
         |
         v
    return bounce buffer DMA address

============================================================
FULL DMA UNMAP FLOW WITH SWIOTLB
============================================================

Driver
 |
 v
dma_unmap_single(dev, dma_addr, size, direction)
 |
 v
swiotlb_unmap_single()
 |
 v
Was bounce buffer used?
 |
 +--> no
 |       |
 |       v
 |   nothing special
 |
 +--> yes
         |
         v
    if DMA_FROM_DEVICE:
         copy bounce buffer back to original buffer
         |
         v
    free bounce slot

============================================================
SCATTER-GATHER FLOW
============================================================

Driver
 |
 v
dma_map_sg()
 |
 v
swiotlb_map_sg()
 |
 v
For each SG entry:
      |
      +--> direct if reachable
      |
      +--> bounce if unreachable

============================================================
COHERENT ALLOCATION FLOW
============================================================

Driver
 |
 v
dma_alloc_coherent()
 |
 v
swiotlb_alloc_coherent()
 |
 v
Allocate memory device can reach
 |
 v
Return CPU address + DMA address

============================================================
SWIOTLB VS IOMMU
============================================================

IOMMU:

    no copy

    device DMA address translated to real memory

------------------------------------------------------------

SWIOTLB:

    copy required when bounce used

    device DMA goes to low buffer

============================================================
PERFORMANCE DIFFERENCE
============================================================

IOMMU:

    Device -> translated memory

Good performance.

------------------------------------------------------------

SWIOTLB:

    Device -> bounce buffer -> CPU copy

Extra memory copy.

Slower.

============================================================
SECURITY DIFFERENCE
============================================================

IOMMU:

    can isolate device DMA

------------------------------------------------------------

SWIOTLB:

    not true isolation

It only ensures device uses reachable memory.

A malicious device may still DMA within reachable low memory.

============================================================
MENTAL MODEL
============================================================

Hardware IOMMU:

    page tables for devices

SWIOTLB:

    temporary low-memory mailbox

No-IOMMU:

    direct physical address

============================================================
ONE-LINE SUMMARY
============================================================

pci-swiotlb.c is the x86-64 glue that enables software
bounce buffering when no hardware IOMMU is available and RAM
extends beyond 4GB; it installs swiotlb_dma_ops so generic DMA
API calls are redirected to lib/swiotlb.c for bounce-buffer
mapping, syncing, and unmapping.
============================================================
```

