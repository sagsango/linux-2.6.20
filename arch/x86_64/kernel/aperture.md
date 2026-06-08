```text
===============================================================================
GART / IOMMU APERTURE FIXUP
File: arch/x86_64/kernel/aperture.c   (Linux 2.6.x style)
===============================================================================

BACKGROUND
==========

This code is for old AMD K8 / x86-64 systems.

Main topic:

    GART IOMMU aperture

GART = Graphics Address Remapping Table
IOMMU = I/O Memory Management Unit

In simple words:

    Device DMA address
          |
          v
    IOMMU / GART translates it
          |
          v
    Real physical RAM address


Why needed?

Some 32-bit PCI/AGP devices cannot DMA above 4GB.

Example:

    System RAM:
        0 GB ---------------- 4 GB ---------------- 8 GB
                              ^
                              |
                          old devices cannot reach above this

If a device can only DMA below 4GB, but the buffer is above 4GB,
the system needs help.

Options:

    1. Bounce buffering
       - copy data through low memory
       - slower

    2. IOMMU/GART remapping
       - device sees low address
       - IOMMU maps it to real RAM
       - faster

This file tries to make option 2 work.


===============================================================================
WHAT IS AN APERTURE?
===============================================================================

An aperture is a reserved physical address window.

Device sees:

    DMA address inside aperture

CPU / IOMMU maps it to:

    Real RAM page somewhere else


Example:

Device DMA Address:

    0xC0000000
          |
          v
+-----------------------------+
| GART Aperture               |
| 64 MB reserved window       |
+-----------------------------+
          |
          v
IOMMU translates to real RAM


Important:

The aperture must be:

    - below 4GB
    - big enough, at least 64MB here
    - not overlapping normal usable RAM
    - programmed consistently in all K8 northbridges


===============================================================================
WHY THIS FILE EXISTS
===============================================================================

The comment says:

    "Firmware replacement code."

Meaning:

    BIOS should configure this aperture correctly.

But some BIOSes are broken:

    - BIOS does not create aperture
    - BIOS creates aperture only in AGP bridge
    - BIOS points aperture into RAM
    - BIOS gives aperture above 4GB
    - BIOS gives different aperture values per CPU/northbridge

So Linux fixes it early during boot.


Why early boot?

Because this may require reserving 32MB / 64MB / 128MB of aligned memory.

Only the bootmem allocator can easily allocate such large aligned memory
early enough.


===============================================================================
IMPORTANT GLOBAL VARIABLES
===============================================================================

int iommu_aperture;

    Set when GART aperture exists.

int iommu_aperture_disabled;

    Disable this logic.

int iommu_aperture_allowed;

    Allow aperture use.

int fallback_aper_order = 1;

    Default fallback aperture size.

    size = 32MB << order
    order = 1
    size = 64MB

int fallback_aper_force;

    Force fallback aperture allocation.

int fix_aperture = 1;

    Enable BIOS aperture fixing.


===============================================================================
APERTURE SIZE FORMULA
===============================================================================

aper_size = 32MB << aper_order

Examples:

    aper_order = 0  -> 32 MB
    aper_order = 1  -> 64 MB
    aper_order = 2  -> 128 MB
    aper_order = 3  -> 256 MB
    aper_order = 4  -> 512 MB
    aper_order = 5  -> 1 GB
    aper_order = 6  -> 2 GB
    aper_order = 7  -> 4 GB


===============================================================================
RESOURCE INSERTION
===============================================================================

Function:

    insert_aperture_resource(aper_base, aper_size)

Purpose:

Tell Linux:

    "This physical address range is reserved for GART."

Code idea:

    gart_resource.start = aper_base;
    gart_resource.end   = aper_base + aper_size - 1;

    insert_resource(&iomem_resource, &gart_resource);


Memory view:

+-----------------------------+
| System Physical Memory Map  |
+-----------------------------+
| RAM                         |
| Reserved                    |
| PCI MMIO                    |
| GART aperture               |
+-----------------------------+


===============================================================================
VALIDATING AN APERTURE
===============================================================================

Function:

    aperture_valid(aper_base, aper_size)

Checks:

1. Base must not be zero

    if (!aper_base)
        invalid

2. Size must be at least 64MB

    if (aper_size < 64MB)
        invalid

3. Must be below 4GB

    if (aper_base + aper_size >= 0xffffffff)
        invalid

4. Must not overlap E820 RAM

    if e820 says this range is RAM
        invalid


Why not overlap RAM?

Because device DMA into aperture would corrupt normal memory.


Diagram:

GOOD:

+------------------+------------------+
| RAM              | GART aperture    |
+------------------+------------------+

BAD:

+------------------+
| RAM              |
|   +----------+   |
|   | Aperture |   |
|   +----------+   |
+------------------+

BAD means normal RAM would be stolen/corrupted.


===============================================================================
ALLOCATING A FALLBACK APERTURE
===============================================================================

Function:

    allocate_aperture()

Purpose:

If BIOS did not provide a valid aperture, Linux allocates one.

Flow:

allocate_aperture()
      |
      +--> choose size
      |
      +--> allocate aligned low memory
      |
      +--> ensure below 4GB
      |
      +--> insert as GART resource
      |
      v
return aperture physical address


Code idea:

    aper_size = 32MB << fallback_aper_order;

    p = __alloc_bootmem_node(node0,
                             aper_size,
                             aper_size,
                             0);

Important:

    allocation size  = aper_size
    alignment        = aper_size

So aperture is naturally aligned.


Why naturally aligned?

Hardware expects aperture base aligned to aperture size.

Example:

    64MB aperture must start at 64MB boundary.


If success:

    Mapping aperture over 65536 KB of RAM @ address

This RAM is lost.

Why?

Linux reserves RAM and uses that address range as fake DMA aperture.


===============================================================================
MANUAL PCI CONFIG ACCESS
===============================================================================

This code runs before normal PCI subsystem is ready.

So it uses low-level PCI config functions:

    read_pci_config()
    write_pci_config()
    read_pci_config_byte()
    read_pci_config_16()

Normal pci_dev structures may not exist yet.


===============================================================================
FINDING PCI CAPABILITY
===============================================================================

Function:

    find_cap(num, slot, func, cap)

Purpose:

Search PCI capability list.

Used to find:

    PCI_CAP_ID_AGP

Flow:

PCI device
   |
   +--> Has capability list?
           |
           +--> No  -> return 0
           |
           +--> Yes -> walk capability list
                         |
                         +--> AGP capability found?
                                 |
                                 +--> return position


===============================================================================
READING AGP APERTURE
===============================================================================

Function:

    read_agp(num, slot, func, cap, order)

Purpose:

Read aperture info from AGP bridge.

Why AGP bridge?

Some BIOSes only configure aperture in AGP bridge,
not in K8 northbridge.

Flow:

read_agp()
    |
    +--> read APSIZE register
    |
    +--> calculate aperture size
    |
    +--> read aperture base from BAR/registers
    |
    +--> validate aperture
    |
    +--> return aperture base


Diagram:

AGP Bridge
   |
   +--> APSIZE register
   |
   +--> Aperture base register
   |
   v
Linux uses this to fix K8 northbridge


===============================================================================
SEARCHING FOR AGP BRIDGE
===============================================================================

Function:

    search_agp_bridge(order, valid_agp)

Purpose:

Manual PCI scan.

Flow:

for bus 0..255
    for slot 0..31
        for function 0..7
            read PCI class
            if host bridge / bridge:
                check AGP capability
                if AGP:
                    valid_agp = 1
                    read_agp()
                    return aperture


ASCII:

PCI Bus Scan
   |
   +--> Bus 0
   |     |
   |     +--> Slot 0
   |     +--> Slot 1
   |     +--> ...
   |
   +--> Bus 1
   |
   +--> ...
   |
   v
Find AGP bridge


===============================================================================
MAIN FUNCTION
===============================================================================

Function:

    iommu_hole_init()

This is the main logic.

High-level flow:

iommu_hole_init()
      |
      +--> check if disabled
      |
      +--> scan K8 northbridges
      |
      +--> read aperture base/size
      |
      +--> validate aperture
      |
      +--> if good, insert resource and return
      |
      +--> if bad, search AGP bridge
      |
      +--> if still bad, allocate fallback aperture
      |
      +--> program all K8 northbridges


===============================================================================
STEP 1 : EARLY EXIT
===============================================================================

Code logic:

if (iommu_aperture_disabled ||
    !fix_aperture ||
    !early_pci_allowed())
        return;


Meaning:

Do nothing if:

    - user disabled aperture
    - fixing disabled
    - early PCI config access not allowed


===============================================================================
STEP 2 : SCAN K8 NORTHBRIDGES
===============================================================================

Code:

for (num = 24; num < 32; num++)

Why 24..31?

K8 northbridge functions live at PCI bus 0,
device numbers 24 to 31.

Each CPU/socket may have one.


Flow:

for each possible K8 northbridge:
    |
    +--> is this really K8 NB?
    |
    +--> read aperture order
    |
    +--> read aperture base
    |
    +--> print CPU aperture info
    |
    +--> validate


Registers:

    0x90 -> aperture control / size order
    0x94 -> aperture base


Aperture base calculation:

    aper_base = read register 0x94
    aper_base &= 0x7fff
    aper_base <<= 25


===============================================================================
STEP 3 : CHECK CONSISTENCY
===============================================================================

All K8 northbridges must agree.

Bad:

CPU0 aperture @ 0xC0000000 size 64MB
CPU1 aperture @ 0xD0000000 size 64MB

Bad:

CPU0 aperture @ 0xC0000000 size 64MB
CPU1 aperture @ 0xC0000000 size 128MB

Good:

CPU0 aperture @ 0xC0000000 size 64MB
CPU1 aperture @ 0xC0000000 size 64MB


If mismatch:

    fix = 1


===============================================================================
STEP 4 : IF BIOS APERTURE IS GOOD
===============================================================================

If:

    fix == 0
    fallback_aper_force == 0

Then:

    insert_aperture_resource(last_aper_base, size);
    return;


Flow:

BIOS aperture valid
      |
      v
Reserve aperture resource
      |
      v
Done


===============================================================================
STEP 5 : TRY AGP BRIDGE
===============================================================================

If BIOS/K8 aperture is bad:

    search_agp_bridge()

Why?

Because BIOS may have configured AGP bridge correctly
but forgot K8 northbridge.

Flow:

Bad K8 aperture
      |
      v
Search AGP bridge
      |
      +--> Found valid aperture?
              |
              +--> Yes: use that base/size
              |
              +--> No: allocate fallback


===============================================================================
STEP 6 : FALLBACK ALLOCATION
===============================================================================

Fallback happens if:

    - no valid BIOS aperture
    - no valid AGP aperture
    - IOMMU needed
    - AGP exists
    - forced by boot parameter

Linux prints:

    Your BIOS doesn't leave a aperture memory hole
    Please enable the IOMMU option in the BIOS setup
    This costs you X MB of RAM


Then:

    allocate_aperture()


Memory before:

+-----------------------------+
| Normal RAM                  |
+-----------------------------+

Memory after:

+-----------------------------+
| Normal RAM                  |
+-----------------------------+
| Reserved GART aperture      |
+-----------------------------+
| Normal RAM                  |
+-----------------------------+

That reserved part is lost as normal RAM.


===============================================================================
STEP 7 : PROGRAM K8 NORTHBRIDGES
===============================================================================

After aperture is chosen:

for every K8 northbridge:

    write_pci_config(0, num, 3, 0x90, aper_order << 1);
    write_pci_config(0, num, 3, 0x94, aper_alloc >> 25);


Meaning:

Tell hardware:

    aperture size = aper_order
    aperture base = aper_alloc


Important comment:

    "Don't enable translation yet. That is done later."

So this only programs base/size.

Actual IOMMU translation is enabled later elsewhere.


===============================================================================
COMPLETE FLOW
===============================================================================

Boot
 |
 v
iommu_hole_init()
 |
 +--> Is aperture fixing enabled?
 |        |
 |        +--> No -> return
 |
 +--> Scan K8 northbridges
 |        |
 |        +--> Read aperture base
 |        +--> Read aperture size
 |        +--> Validate
 |
 +--> Good BIOS aperture?
 |        |
 |        +--> Yes
 |        |     |
 |        |     +--> insert resource
 |        |     +--> return
 |        |
 |        +--> No
 |
 +--> Search AGP bridge
 |        |
 |        +--> Found valid AGP aperture?
 |              |
 |              +--> Yes -> use it
 |              |
 |              +--> No
 |
 +--> Need IOMMU / AGP / forced?
 |        |
 |        +--> No -> return
 |        |
 |        +--> Yes
 |
 +--> allocate_aperture()
 |        |
 |        +--> reserve low aligned RAM
 |        +--> mark it as GART resource
 |
 +--> Program all K8 northbridges
 |
 v
Done


===============================================================================
WHY THIS IS CHEAPER THAN BOUNCE BUFFERING
===============================================================================

Bounce buffering:

Device cannot access high RAM.

So kernel does:

    High RAM buffer
         |
         v
    Copy to low RAM bounce buffer
         |
         v
    Device DMA

For input:

    Device DMA to low RAM bounce buffer
         |
         v
    Copy to high RAM buffer

This causes extra memory copies.


With GART/IOMMU:

Device sees aperture address:

    Device DMA address
          |
          v
    GART aperture
          |
          v
    translated to real RAM

No extra copy.

That is why the comment says:

    "This is cheaper than doing bounce buffering."


===============================================================================
KEY IDEA
===============================================================================

This file fixes broken firmware.

BIOS should reserve and program a valid GART aperture.

If BIOS fails, Linux:

    1. Checks existing K8 aperture
    2. Searches AGP bridge aperture
    3. Allocates fallback low memory hole
    4. Programs all K8 northbridges

Final goal:

    Make DMA remapping possible for devices that cannot directly
    reach all physical memory.
===============================================================================
```

