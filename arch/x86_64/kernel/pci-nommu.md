============================================================
x86-64 NO-IOMMU DMA BACKEND
File: arch/x86_64/kernel/pci-nommu.c
Linux 2.6.x Era
============================================================

PURPOSE
------------------------------------------------------------

This file provides fallback DMA mapping operations when no
hardware IOMMU backend is active.

It is the simplest possible DMA backend:

    DMA address = physical / bus address

No translation.
No bounce buffering.
No TCE/GART table.
No IOMMU protection.

Used when:

    Calgary IOMMU not active
    GART IOMMU not active
    SWIOTLB not active
    dma_ops not already installed

============================================================
BACKGROUND: DMA WITHOUT IOMMU
============================================================

With IOMMU:

Device DMA address
      |
      v
IOMMU table
      |
      v
Physical RAM

Without IOMMU:

Device DMA address
      |
      v
Physical RAM directly

So mapping is just:

    virt_to_bus(ptr)

============================================================
BIG IDEA
------------------------------------------------------------

This file does not create mappings.

It only checks:

    Can the device address this memory?

If yes:

    return direct bus address

If no:

    return bad_dma_address

============================================================
KEY LIMITATION
============================================================

If a device can only DMA to 32-bit addresses:

    max address = 0xffffffff

and the buffer is above 4GB:

    direct DMA cannot work

Without IOMMU/SWIOTLB, Linux has no way to fix it.

============================================================
IMPORTANT FUNCTIONS
============================================================

check_addr()

    validate DMA address fits device mask

nommu_map_single()

    map one buffer

nommu_unmap_single()

    no-op

nommu_map_sg()

    map scatterlist entries

nommu_unmap_sg()

    no-op

no_iommu_init()

    install nommu dma_ops

============================================================
1. check_addr()
============================================================

Purpose:

    Verify DMA range is addressable by device.

Input:

    bus
    size
    hwdev->dma_mask

Check:

    bus + size <= *dma_mask

If overflow:

    print error
    return 0

Otherwise:

    return 1

============================================================
DMA MASK EXAMPLE
============================================================

Device:

    32-bit DMA mask

Mask:

    0xffffffff

Buffer:

    bus = 0x100000000
    size = 4096

Check:

    0x100000000 + 4096 > 0xffffffff

Result:

    invalid

============================================================
2. nommu_map_single()
============================================================

Purpose:

    Map one contiguous CPU buffer for device DMA.

Code logic:

    bus = virt_to_bus(ptr)

    if check_addr() fails:
        return bad_dma_address

    return bus

============================================================
FLOW
============================================================

Driver
   |
   v
dma_map_single()
   |
   v
nommu_map_single()
   |
   v
virt_to_bus(ptr)
   |
   v
check against dma_mask
   |
   +--> fits
   |       |
   |       v
   |   return bus address
   |
   +--> does not fit
           |
           v
       return bad_dma_address

============================================================
3. nommu_unmap_single()
============================================================

Purpose:

    Unmap single DMA mapping.

But because there was no mapping:

    function does nothing

No IOMMU table entry to remove.

No TLB flush.

No bounce buffer to copy back.

============================================================
4. nommu_map_sg()
============================================================

Purpose:

    Map scatter-gather list directly.

Each SG entry:

    page + offset
        |
        v
    virt_to_bus()
        |
        v
    dma_address

============================================================
FLOW
============================================================

for each sg entry:

    ensure page exists

    dma_address =
        virt_to_bus(page_address(page) + offset)

    check dma mask

    dma_length = length

Return:

    nents

============================================================
SCATTER-GATHER EXAMPLE
============================================================

SG[0]:

    page A + offset 0
    length 4096

SG[1]:

    page B + offset 128
    length 2048

Output:

SG[0].dma_address = direct bus address
SG[0].dma_length  = 4096

SG[1].dma_address = direct bus address
SG[1].dma_length  = 2048

============================================================
5. nommu_unmap_sg()
============================================================

No-op.

Reason:

    nothing was allocated
    nothing was translated
    nothing needs flushing

============================================================
6. nommu_dma_ops
============================================================

This backend registers:

    map_single      = nommu_map_single
    unmap_single    = nommu_unmap_single
    map_sg          = nommu_map_sg
    unmap_sg        = nommu_unmap_sg
    is_phys         = 1

============================================================
is_phys = 1
============================================================

Meaning:

    DMA addresses are physical/bus addresses.

No translation layer.

============================================================
7. no_iommu_init()
============================================================

Purpose:

    Install fallback DMA backend.

Flow:

    if dma_ops already exists:
        return

    force_iommu = 0

    dma_ops = &nommu_dma_ops

============================================================
WHY CHECK dma_ops FIRST?
============================================================

Another backend may already be active:

    Calgary
    GART
    SWIOTLB

If so:

    do not override it.

============================================================
WHERE THIS FITS IN BOOT FLOW
============================================================

From pci-dma.c:

    pci_iommu_init()
        |
        +--> calgary_iommu_init()
        |
        +--> gart_iommu_init()
        |
        +--> no_iommu_init()

So no_iommu is the final fallback.

============================================================
COMPLETE BOOT FLOW
============================================================

Boot
 |
 v
Try Calgary IOMMU
 |
 +--> success?
 |       |
 |       v
 |   dma_ops = calgary_dma_ops
 |
 v
Try GART IOMMU
 |
 +--> success?
 |       |
 |       v
 |   dma_ops = gart_dma_ops
 |
 v
Try no-IOMMU fallback
 |
 v
dma_ops = nommu_dma_ops

============================================================
COMPLETE DMA FLOW
============================================================

Driver
 |
 v
dma_map_single(dev, ptr, size)
 |
 v
dma_ops->map_single()
 |
 v
nommu_map_single()
 |
 v
virt_to_bus(ptr)
 |
 v
check_addr()
 |
 +--> address fits device mask
 |       |
 |       v
 |   device gets direct bus address
 |
 +--> address too high
         |
         v
     bad_dma_address

============================================================
WHAT CAN GO WRONG?
============================================================

Case:

    RAM > 4GB

Device:

    only 32-bit DMA

Backend:

    no IOMMU

Buffer:

    above 4GB

Result:

    mapping fails

or driver mishandles failure.

============================================================
WHY THIS IS DANGEROUS
============================================================

No-IOMMU mode has no protection.

A buggy device can DMA anywhere it can address.

Example:

    device writes to wrong DMA address

Could corrupt:

    kernel text
    page tables
    user memory
    filesystem buffers

IOMMU prevents this.

No-IOMMU does not.

============================================================
WHEN NO-IOMMU IS OKAY
============================================================

Usually okay if:

    RAM is below device DMA mask

and

    devices are trusted

Example:

    2GB RAM
    32-bit PCI devices

All physical memory is below 4GB.

Direct DMA works.

============================================================
WHEN NO-IOMMU IS NOT OKAY
============================================================

Risky if:

    RAM above 4GB

    old 32-bit DMA devices

    device passthrough

    untrusted PCIe devices

    buggy firmware

    security isolation required

============================================================
MENTAL MODEL
============================================================

Calgary/GART/SWIOTLB:

    create or emulate DMA translations

No-IOMMU:

    "Here is the physical address. Good luck."

============================================================
ONE-LINE SUMMARY
============================================================

pci-nommu.c is the final x86-64 DMA fallback backend:
it maps DMA by directly returning virt_to_bus() addresses,
checks that the address fits the device DMA mask, and does
nothing on unmap because no real IOMMU mapping exists.
============================================================
