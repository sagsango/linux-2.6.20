```text
===============================================================================
EARLY CHIPSET QUIRKS
File: arch/x86_64/kernel/early-quirks.c
===============================================================================

PURPOSE
=======

This file applies very early chipset workarounds.

It runs before the normal PCI subsystem is initialized.

Why?

Some chipset bugs must be fixed before:

    - timers are initialized
    - IOMMU is initialized
    - ACPI interrupt routing is trusted
    - normal PCI quirks can run


This file is only for early, chipset-wide bugs.

Normal PCI device quirks should go into regular PCI quirk code.


===============================================================================
BACKGROUND
==========

A chipset controls important platform behavior:

    - PCI bridges
    - interrupt routing
    - timers
    - IOMMU / DMA behavior
    - ACPI behavior


Sometimes firmware/BIOS configures these incorrectly.

Linux needs to detect the chipset early and apply workarounds.


===============================================================================
WHY NOT USE NORMAL PCI SUBSYSTEM?
===============================================================================

At this point in boot:

    struct pci_dev does not exist yet
    PCI bus scan is not complete
    drivers are not loaded
    timers may not work yet


So this file uses direct PCI config access:

    read_pci_config()
    read_pci_config_16()
    read_pci_config_byte()


This is a "poor man's PCI scan".


===============================================================================
MAIN FLOW
===============================================================================

early_quirks()
      |
      +--> check early PCI access allowed
      |
      +--> scan bus/slot/function manually
      |
      +--> look for PCI bridge devices
      |
      +--> read vendor ID
      |
      +--> match vendor in early_qrk[]
      |
      +--> call chipset workaround
      |
      v
return after first match


===============================================================================
CHIPSET TABLE
===============================================================================

struct chipset {
    u16 vendor;
    void (*f)(void);
};


Table:

early_qrk[] =
    NVIDIA  -> nvidia_bugs()
    VIA     -> via_bugs()
    ATI     -> ati_bugs()
    INTEL   -> intel_bugs()


Meaning:

    If PCI bridge vendor is NVIDIA, run NVIDIA workaround.
    If PCI bridge vendor is VIA, run VIA workaround.
    etc.


===============================================================================
MANUAL PCI DISCOVERY
===============================================================================

Function:

    early_quirks()

Loop:

for bus 0..31
    for slot 0..31
        for function 0..7
            read class
            if no device:
                break

            if class is PCI bridge:
                read vendor
                apply matching quirk

            if not multifunction:
                break


ASCII:

PCI Scan
   |
   +--> Bus 0
   |     |
   |     +--> Slot 0
   |     |     |
   |     |     +--> Function 0
   |     |     +--> Function 1
   |     |
   |     +--> Slot 1
   |
   +--> Bus 1
   |
   +--> ...
   |
   v
Find chipset bridge


===============================================================================
WHY LOOK FOR PCI BRIDGE?
===============================================================================

Chipset identity is usually visible through host/PCI bridge devices.

Example:

    Vendor ID = NVIDIA
    Vendor ID = VIA
    Vendor ID = ATI
    Vendor ID = INTEL


This identifies the platform chipset early.


===============================================================================
VIA WORKAROUND
===============================================================================

Function:

    via_bugs()

Problem:

Some VIA chipsets had broken/unsafe IOMMU aperture behavior.


Logic:

if system has RAM above 4GB or force_iommu is set:
    if iommu aperture is not explicitly allowed:
        disable IOMMU aperture


Code idea:

    if ((end_pfn > MAX_DMA32_PFN || force_iommu) &&
        !iommu_aperture_allowed) {
        iommu_aperture_disabled = 1;
    }


Meaning:

    Looks like VIA chipset.
    Disable IOMMU unless user says:

        iommu=allowed


Why?

Because using broken IOMMU setup can corrupt DMA or memory.


===============================================================================
NVIDIA WORKAROUND
===============================================================================

Function:

    nvidia_bugs()

Problem:

NVIDIA boards often report wrong ACPI timer overrides unless HPET exists.


Background:

ACPI may contain interrupt override entries.

These tell Linux:

    "Timer IRQ is routed differently than legacy IRQ0"


But on some NVIDIA boards:

    override is wrong

Using it can break timer interrupts.


Logic:

if user forced acpi_use_timer_override:
    trust override

else:
    check if ACPI HPET table exists

if HPET not detected:
    skip ACPI timer override


Code idea:

    acpi_table_parse(ACPI_HPET, nvidia_hpet_check);

    if no HPET:
        acpi_skip_timer_override = 1;


Message:

    Nvidia board detected.
    Ignoring ACPI timer override.
    If you got timer trouble try acpi_use_timer_override


===============================================================================
WHY HPET MATTERS HERE
===============================================================================

HPET = High Precision Event Timer

If HPET exists, timer routing situation may be different.

If HPET does not exist, Linux avoids trusting the NVIDIA ACPI timer override.


Flow:

NVIDIA chipset
      |
      v
ACPI HPET table exists?
      |
      +--> yes -> do not skip timer override
      |
      +--> no  -> skip timer override


===============================================================================
ATI WORKAROUND
===============================================================================

Function:

    ati_bugs()

Problem:

Timer routing over 8254 is problematic on some ATI boards.


Logic:

if timer_over_8254 == 1:
    timer_over_8254 = 0


Meaning:

    Disable timer routing over legacy PIT/8254 path.


Message:

    ATI board detected.
    Disabling timer routing over 8254.


===============================================================================
INTEL WORKAROUND
===============================================================================

Function:

    intel_bugs()

Reads Intel host bridge device ID:

    read_pci_config_16(0, 0, 0, PCI_DEVICE_ID)


If chipset is:

    Intel E7320
    Intel E7520
    Intel E7525

then:

    quirk_intel_irqbalance()


Purpose:

Apply special IRQ balancing workaround for these Intel server chipsets.


Flow:

Intel chipset
      |
      v
Read device ID
      |
      v
E7320/E7520/E7525?
      |
      +--> yes -> quirk_intel_irqbalance()
      |
      +--> no  -> return


===============================================================================
EARLY PCI ACCESS CHECK
===============================================================================

Code:

    if (!early_pci_allowed())
        return;


Meaning:

Do not touch PCI config space if early PCI access is unsafe or disabled.


===============================================================================
MULTIFUNCTION DEVICE CHECK
===============================================================================

Code:

    type = read_pci_config_byte(... PCI_HEADER_TYPE);

    if (!(type & 0x80))
        break;


Meaning:

If function 0 says device is not multifunction,
do not scan functions 1..7.


===============================================================================
COMPLETE FLOW
===============================================================================

Kernel Early Boot
      |
      v
early_quirks()
      |
      +--> early PCI access allowed?
      |        |
      |        +--> no -> return
      |
      +--> scan PCI buses 0..31
      |
      +--> find PCI bridge
      |
      +--> read vendor ID
      |
      +--> vendor NVIDIA?
      |        |
      |        +--> nvidia_bugs()
      |
      +--> vendor VIA?
      |        |
      |        +--> via_bugs()
      |
      +--> vendor ATI?
      |        |
      |        +--> ati_bugs()
      |
      +--> vendor INTEL?
               |
               +--> intel_bugs()
      |
      v
return


===============================================================================
RELATION TO OTHER FILES
===============================================================================

aperture.c
-----------
VIA workaround can disable IOMMU aperture logic.

    via_bugs()
        |
        v
    iommu_aperture_disabled = 1


apic.c / io_apic.c
------------------
NVIDIA and ATI workarounds affect timer interrupt routing.

    nvidia_bugs()
        |
        v
    acpi_skip_timer_override = 1

    ati_bugs()
        |
        v
    timer_over_8254 = 0


ACPI
----
NVIDIA quirk checks ACPI HPET table.

    acpi_table_parse(ACPI_HPET, ...)


===============================================================================
KEY IDEA
===============================================================================

This file is a tiny early boot chipset bug database.

It runs before normal PCI exists and applies only urgent platform fixes.

It prevents Linux from trusting broken firmware/chipset behavior for:

    - IOMMU aperture
    - ACPI timer overrides
    - legacy timer routing
    - Intel IRQ balancing

It is intentionally small because most quirks should happen later in the
normal PCI subsystem.
===============================================================================
```

