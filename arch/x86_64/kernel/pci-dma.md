```text
============================================================
LINUX x86-64 DYNAMIC DMA MAPPING CORE
File: arch/x86_64/kernel/pci-dma.c
============================================================

PURPOSE
------------------------------------------------------------

This file is the central x86-64 DMA mapping dispatcher.

It does not implement only one IOMMU.

Instead, it chooses and coordinates different DMA backends:

    - no IOMMU
    - GART IOMMU
    - Calgary IOMMU
    - SWIOTLB software bounce buffering

It provides common DMA APIs used by drivers:

    dma_alloc_coherent()
    dma_free_coherent()
    dma_supported()
    dma_set_mask()

============================================================
BACKGROUND: WHAT IS DMA?
============================================================

DMA = Direct Memory Access.

A device can read/write RAM without CPU copying data.

Example:

    NIC receives packet
        |
        v
    NIC DMA writes packet into RAM

Driver gives device a DMA address.

Device uses that address on the PCI bus.

============================================================
THE CORE PROBLEM
============================================================

CPU uses virtual addresses:

    CPU virtual address
        |
        v
    physical address

Device does not use CPU virtual addresses.

Device uses:

    DMA / bus address

Depending on platform:

    DMA address may equal physical address

or

    DMA address may be translated by IOMMU

or

    DMA may require bounce buffers

============================================================
BIG PICTURE
============================================================

Driver
   |
   v
dma_alloc_coherent()
dma_map_single()
dma_map_sg()
   |
   v
x86-64 DMA layer
   |
   +--> no_iommu
   |
   +--> GART IOMMU
   |
   +--> Calgary IOMMU
   |
   +--> SWIOTLB
   |
   v
device-visible DMA address

============================================================
IMPORTANT GLOBAL FLAGS
============================================================

iommu_merge

    Enable IOMMU scatter-gather merging.

------------------------------------------------------------

iommu_bio_merge

    Tell block layer to assume DMA merging.

------------------------------------------------------------

iommu_sac_force

    Force SAC-style 32-bit DMA in some cases.

------------------------------------------------------------

no_iommu

    Disable hardware IOMMU usage.

------------------------------------------------------------

force_iommu

    Force IOMMU even when direct DMA might work.

------------------------------------------------------------

panic_on_overflow

    Panic if IOMMU space overflows.

------------------------------------------------------------

iommu_detected

    Set when hardware IOMMU is found.

------------------------------------------------------------

bad_dma_address

    Special invalid DMA address.

============================================================
SAC vs DAC
============================================================

SAC = Single Address Cycle

    32-bit PCI DMA address

    address <= 4GB

------------------------------------------------------------

DAC = Dual Address Cycle

    64-bit PCI DMA address

    address > 4GB

Some old devices support only SAC.

Some support DAC.

============================================================
fallback_dev
============================================================

Used when caller passes:

    dev == NULL

It pretends to be a generic 32-bit DMA device:

    coherent_dma_mask = DMA_32BIT_MASK

This is old compatibility behavior.

============================================================
DMA MASK
============================================================

A device has a DMA mask.

Example:

    24-bit mask  -> can DMA below 16MB
    32-bit mask  -> can DMA below 4GB
    40-bit mask  -> can DMA below 1TB
    64-bit mask  -> can DMA almost anywhere

Driver calls:

    dma_set_mask(dev, mask)

============================================================
dma_supported()
============================================================

Checks whether a device can use a given DMA mask.

Flow:

    if DAC forbidden and mask > 32-bit:
        reject

    if dma_ops has dma_supported:
        ask backend

    if mask < 24-bit:
        reject

    if iommu_sac_force and mask >= 40-bit:
        reject DAC

    otherwise:
        supported

============================================================
dma_set_mask()
============================================================

Flow:

    if device has no dma_mask:
        fail

    if dma_supported(dev, mask) fails:
        fail

    dev->dma_mask = mask

Meaning:

    Driver tells kernel what addresses device can handle.

============================================================
dma_alloc_pages()
============================================================

Allocates pages near the device.

NUMA-aware behavior:

    if PCI device:
        node = pcibus_to_node(dev->bus)

    else:
        node = current NUMA node

Then:

    alloc_pages_node(node, gfp, order)

Meaning:

    Try to allocate memory close to the device.

============================================================
dma_alloc_coherent()
============================================================

This is the most important function in this file.

Purpose:

    Allocate memory shared by CPU and device.

Returns two addresses:

    CPU virtual address

and

    DMA address

Example:

    void *cpu_addr;
    dma_addr_t dma_addr;

    cpu_addr = dma_alloc_coherent(dev, size, &dma_addr, GFP_KERNEL);

CPU uses:

    cpu_addr

Device uses:

    dma_addr

============================================================
COHERENT DMA MEMORY
============================================================

Coherent means:

    CPU and device see consistent memory

No explicit cache sync needed.

Common use:

    descriptor rings
    command queues
    completion queues

Example:

    NIC RX ring
    NVMe submission queue
    USB descriptor table

============================================================
dma_alloc_coherent() FLOW
============================================================

dma_alloc_coherent()
    |
    +--> choose device or fallback_dev
    |
    +--> get coherent_dma_mask
    |
    +--> add __GFP_NORETRY
    |
    +--> choose zone:
    |       GFP_DMA32 or GFP_DMA
    |
    +--> allocate pages
    |
    +--> check if physical/bus address fits mask
    |
    +--> if direct DMA works:
    |       return virt_to_bus(memory)
    |
    +--> else use dma_ops backend:
            Calgary / GART / SWIOTLB / no_iommu

============================================================
WHY GFP_DMA32?
============================================================

If device can only DMA below 4GB:

    use GFP_DMA32

This tries to allocate memory from the 32-bit DMA zone.

============================================================
WHY GFP_DMA?
============================================================

If device needs very low memory, like below 16MB:

    use GFP_DMA

But this zone is precious.

So code avoids GFP_DMA unless absolutely needed.

============================================================
DIRECT DMA CASE
============================================================

If allocated memory is already addressable by device:

    dma_handle = virt_to_bus(memory)

No IOMMU mapping needed.

ASCII:

Device DMA address
        |
        v
Physical RAM directly

============================================================
IOMMU CASE
============================================================

If memory is too high for device mask:

    use dma_ops

Example:

Device supports only 32-bit DMA.

Memory allocated above 4GB.

Then:

    IOMMU maps low DMA address -> high physical memory

ASCII:

Device sees:

    0x100000

IOMMU maps to:

    0x123456000

============================================================
force_iommu CASE
============================================================

If force_iommu is set:

    even direct-addressable memory uses IOMMU

Useful for:

    debugging
    DMA isolation
    testing IOMMU code
    forcing consistent behavior

============================================================
dma_ops
============================================================

This file dispatches through:

    dma_ops

Example methods:

    alloc_coherent
    map_single
    unmap_single
    map_sg
    unmap_sg
    dma_supported

Backends install their own dma_ops.

Example:

    Calgary sets:

        dma_ops = &calgary_dma_ops

============================================================
dma_free_coherent()
============================================================

Flow:

    if dma_ops->unmap_single exists:
        unmap DMA mapping

    free_pages()

Important:

    Caller must ensure device is no longer using memory.

Otherwise:

    device may DMA into freed memory

============================================================
IOMMU BOOT OPTIONS
============================================================

Kernel command line:

    iommu=...

This file parses many options.

Examples:

    iommu=off

        disable IOMMU

------------------------------------------------------------

    iommu=force

        force IOMMU usage

------------------------------------------------------------

    iommu=soft

        use SWIOTLB bounce buffering

------------------------------------------------------------

    iommu=calgary

        enable Calgary IOMMU

------------------------------------------------------------

    iommu=merge

        enable IOMMU SG merging

------------------------------------------------------------

    iommu=biomerge

        enable block-layer merge assumptions

------------------------------------------------------------

    iommu=panic

        panic on IOMMU overflow

------------------------------------------------------------

    iommu=nodac

        forbid 64-bit DMA

------------------------------------------------------------

    iommu=allowdac

        allow 64-bit DMA

============================================================
iommu_setup()
============================================================

Parses:

    iommu=

Flow:

    while options remain:
        parse option
        update global flags
        pass options to GART
        pass options to Calgary
        pass options to SWIOTLB

============================================================
EARLY IOMMU ALLOCATION FLOW
============================================================

Function:

    pci_iommu_alloc()

Called early.

Order matters:

    1. iommu_hole_init()
    2. detect_calgary()
    3. pci_swiotlb_init()

============================================================
WHY ORDER MATTERS?
============================================================

The kernel tries hardware IOMMU first.

If unavailable or unsuitable:

    fall back to SWIOTLB

Order:

    GART detection/prep
    Calgary detection
    SWIOTLB fallback

============================================================
pci_iommu_init()
============================================================

Runs later:

    fs_initcall(pci_iommu_init)

Flow:

    calgary_iommu_init()
    gart_iommu_init()
    no_iommu_init()

Finally one backend becomes active.

============================================================
FULL BOOT FLOW
============================================================

Kernel boot
    |
    v
parse iommu= command line
    |
    v
pci_iommu_alloc()
    |
    +--> prepare GART
    +--> detect Calgary
    +--> prepare SWIOTLB
    |
    v
PCI subsystem initializes
    |
    v
pci_iommu_init()
    |
    +--> try Calgary
    +--> try GART
    +--> fallback no_iommu
    |
    v
dma_ops selected

============================================================
FULL DRIVER FLOW
============================================================

Driver probe
    |
    v
dma_set_mask(dev, DMA_64BIT_MASK)
    |
    v
dma_supported()
    |
    v
driver allocates coherent ring
    |
    v
dma_alloc_coherent()
    |
    +--> direct mapping if possible
    |
    +--> IOMMU mapping if needed
    |
    +--> SWIOTLB if needed
    |
    v
driver programs device with dma_handle

============================================================
EXAMPLE: 32-bit DEVICE, RAM ABOVE 4GB
============================================================

Device mask:

    0xffffffff

Memory allocated:

    physical 0x123456000

Problem:

    device cannot address this directly

Solution:

    IOMMU maps:

        DMA 0x00100000 -> PA 0x123456000

Driver gives device:

    0x00100000

Device works.

============================================================
EXAMPLE: NO IOMMU, 32-bit DEVICE
============================================================

Device mask:

    0xffffffff

Memory above 4GB:

    not usable directly

Possible solutions:

    allocate from DMA32 zone

or

    SWIOTLB bounce buffer

If none available:

    allocation fails

============================================================
SWIOTLB BACKGROUND
============================================================

SWIOTLB = Software I/O TLB.

It is not a hardware IOMMU.

It uses bounce buffers.

Flow:

    high memory buffer
        |
        v
    copy to low memory bounce buffer
        |
        v
    device DMA to low memory

Good fallback for devices with limited DMA masks.

============================================================
GART BACKGROUND
============================================================

AMD64 systems had GART aperture.

Originally for AGP.

Linux reused it as simple IOMMU.

Flow:

    DMA address in aperture
        |
        v
    GART translation table
        |
        v
    physical RAM

============================================================
CALGARY BACKGROUND
============================================================

IBM Calgary is hardware DMA remapping.

Flow:

    DMA address
        |
        v
    Calgary TCE table
        |
        v
    physical RAM

This file calls:

    detect_calgary()
    calgary_iommu_init()

but Calgary implementation lives elsewhere.

============================================================
NO_IOMMU BACKGROUND
============================================================

If no IOMMU is active:

    device DMA address == bus/physical address

Fast but unsafe.

Works only if:

    device can address memory
    platform has simple physical bus addressing
    no isolation required

============================================================
IMPORTANT DESIGN POINT
============================================================

Drivers do not care which backend is active.

Driver calls:

    dma_alloc_coherent()
    dma_map_single()
    dma_map_sg()

Architecture code chooses:

    direct
    IOMMU
    bounce buffering

This abstraction is the whole point of Linux DMA API.

============================================================
MENTAL MODEL
============================================================

This file is not the IOMMU itself.

It is the traffic controller.

Driver asks:

    "Give me DMA memory."

This file decides:

    "Can the device use physical address directly?"

If yes:

    return virt_to_bus()

If no:

    ask dma_ops backend to map it

If forced:

    ask dma_ops anyway

If impossible:

    fail or panic

============================================================
ONE-LINE SUMMARY
============================================================

pci-dma.c is the x86-64 DMA mapping coordinator: it parses
IOMMU policy, chooses the active DMA backend, allocates
coherent DMA memory near the device, checks DMA masks, falls
back to hardware IOMMU or SWIOTLB when direct DMA is not safe,
and exposes the common DMA API used by all device drivers.
============================================================
```

