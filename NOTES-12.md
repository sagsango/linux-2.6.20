# PCI vs ACPI
================================================================================
                PCI vs ACPI — COMPLETE ARCHITECTURAL ASCII NOTES
================================================================================

This document explains PCI and ACPI from firmware to Linux kernel (2.6.x era),
including discovery, enumeration, IRQs, power management, and driver binding.

-------------------------------------------------------------------------------
SECTION 0: ONE-LINE MENTAL MODEL
-------------------------------------------------------------------------------

PCI  = hardware bus + self-describing devices (data / performance path)
ACPI = firmware tables + bytecode (control / power / policy path)

-------------------------------------------------------------------------------
SECTION 1: WHAT THEY ARE (FUNDAMENTAL NATURE)
-------------------------------------------------------------------------------

PCI:
----
- Physical hardware bus
- Devices exist independently of firmware
- Devices expose configuration space
- OS probes hardware to discover devices
- Designed for high-performance I/O

ACPI:
-----
- Firmware-to-OS interface
- Devices are described, not discovered
- Uses tables + AML bytecode
- OS executes firmware-provided methods
- Designed for power, control, policy

-------------------------------------------------------------------------------
SECTION 2: BOOT-TIME POSITIONING (WHO COMES FIRST)
-------------------------------------------------------------------------------

POWER ON
 |
 v
Firmware (BIOS / UEFI)
 |
 |-- Initialize chipset
 |-- Initialize PCI Root Complex
 |-- Build ACPI tables:
 |     RSDP
 |     RSDT / XSDT
 |     FADT
 |     MADT
 |     DSDT
 |     SSDT(s)
 |
 v
Bootloader
 |
 v
Linux Kernel
 |
 |-- acpi_init()
 |
 |-- init_IRQ()
 |
 |-- pci_init()
 |
 v
Drivers bind

IMPORTANT ORDERING:
-------------------
ACPI is parsed FIRST
PCI is scanned AFTER

-------------------------------------------------------------------------------
SECTION 3: PCI — HARDWARE-DRIVEN DISCOVERY (DETAILED)
-------------------------------------------------------------------------------

3.1 PCI TOPOLOGY
----------------

PCI Root Complex
 |
 +-- Bus 0
      |
      +-- Device 0
      |     |
      |     +-- Function 0
      |     +-- Function 1
      |
      +-- Device 1
            |
            +-- Function 0

- Bus:    0..255
- Device: 0..31
- Func:   0..7

-------------------------------------------------------------------------------

3.2 PCI ENUMERATION ALGORITHM (ACTUAL LOGIC)
--------------------------------------------

for bus = 0..255
  for device = 0..31
    for function = 0..7
      read PCI config offset 0x00
      if vendor_id != 0xFFFF
        device exists
        allocate struct pci_dev

NO firmware involvement needed.
Hardware responds directly.

-------------------------------------------------------------------------------

3.3 PCI CONFIGURATION SPACE (PER FUNCTION)
------------------------------------------

Offset   Meaning
------   ----------------------------------
0x00     Vendor ID
0x02     Device ID
0x04     Command
0x06     Status
0x08     Class Code
0x0C     Cache Line Size
0x10     BAR0
0x14     BAR1
0x18     BAR2
0x1C     BAR3
0x20     BAR4
0x24     BAR5
0x3C     Interrupt Line
0x3D     Interrupt Pin

BARs describe:
- MMIO regions
- PIO regions
- DMA-visible memory

-------------------------------------------------------------------------------

3.4 LINUX PCI OBJECT MODEL
--------------------------

struct pci_dev
 |
 +-- vendor
 +-- device
 +-- class
 +-- revision
 +-- resource[BARs]
 +-- irq
 +-- bus
 +-- devfn

Driver binding:

struct pci_driver
 |
 +-- id_table[]   (vendor/device/class)
 +-- probe()
 +-- remove()

-------------------------------------------------------------------------------
SECTION 4: ACPI — FIRMWARE-DRIVEN DESCRIPTION (DETAILED)
-------------------------------------------------------------------------------

4.1 ACPI TABLE HIERARCHY
-----------------------

RSDP  (Root System Description Pointer)
 |
 v
RSDT / XSDT
 |
 +-- FADT   (fixed hardware info)
 +-- MADT   (interrupt topology)
 +-- DSDT   (main AML bytecode)
 +-- SSDT   (supplemental AML)

-------------------------------------------------------------------------------

4.2 ACPI NAMESPACE (LOGICAL TREE)
---------------------------------

\
 +-- _SB_                    (System Bus)
 |    |
 |    +-- PCI0               (PCI Root Bridge)
 |    |    |
 |    |    +-- RP01          (Root Port)
 |    |    +-- RP02
 |    |
 |    +-- EC0                (Embedded Controller)
 |    |
 |    +-- BAT0               (Battery)
 |    |
 |    +-- PWRB               (Power Button)
 |    |
 |    +-- TZ00               (Thermal Zone)

-------------------------------------------------------------------------------

4.3 ACPI DEVICE IDENTITY
------------------------

_HID examples:
--------------
PNP0A08   -> PCI Express Root Bridge
PNP0C09   -> Embedded Controller
PNP0C0A   -> Battery
PNP0C0C   -> Power Button
PNP0C0E   -> Sleep Button

No probing.
If firmware does not list it, OS does not invent it.

-------------------------------------------------------------------------------

4.4 ACPI AML EXECUTION MODEL
----------------------------

ACPI tables contain AML bytecode.
Linux includes an AML interpreter.

Runtime methods:

_STA()   -> Is device present?
_CRS()   -> What resources does it use?
_PRS()   -> Possible resources
_PS0()   -> Power ON
_PS3()   -> Power OFF
_WAK()   -> Wake from sleep

Firmware logic runs INSIDE the OS.

-------------------------------------------------------------------------------
SECTION 5: HOW ACPI AND PCI WORK TOGETHER (CRITICAL)
-------------------------------------------------------------------------------

5.1 ACPI DESCRIBES PCI — DOES NOT REPLACE IT
--------------------------------------------

ACPI Device (PCI0)
{
  _HID = "PNP0A08"    // PCI Root
  _SEG = 0
  _BBN = 0
}

Meaning:
--------
Firmware says:
"There is a PCI root bridge here"

Then:
------
PCI scanning discovers actual devices below it.

-------------------------------------------------------------------------------

5.2 COMBINED MODEL (ASCII)
-------------------------

ACPI Firmware Description
 |
 +-- PCI Root Bridge (PNP0A08)
 |     |
 |     +-- PCI Bus 0
 |           |
 |           +-- PCI Device: GPU
 |           +-- PCI Device: NIC
 |
 +-- ACPI-only Devices
       |
       +-- Embedded Controller
       +-- Battery
       +-- Power Button
       +-- Thermal Zone

-------------------------------------------------------------------------------
SECTION 6: INTERRUPTS (MAJOR DIFFERENCE)
-------------------------------------------------------------------------------

6.1 PCI INTERRUPTS
------------------

Modern (MSI / MSI-X):

PCI Device
 |
 +-- MSI vector
 |
 v
Local APIC
 |
 v
CPU

No ACPI involvement at runtime.

--------------------------------------

Legacy (INTx):

PCI Device
 |
 +-- INTA/B/C/D
 |
 v
IO-APIC
 |
 v
CPU

-------------------------------------------------------------------------------

6.2 ACPI INTERRUPTS (GSI MODEL)
-------------------------------

ACPI Device
 |
 +-- _CRS reports GSI = N
 |
 v
IO-APIC pin N
 |
 v
Linux IRQ number
 |
 v
irq_desc[N]

ACPI DESCRIBES interrupt topology.
Kernel MAPS it.

-------------------------------------------------------------------------------
SECTION 7: POWER MANAGEMENT (ACPI DOMINATES)
-------------------------------------------------------------------------------

7.1 PCI POWER STATES
-------------------

D0  -> fully on
D3  -> off

Limited scope.

-------------------------------------------------------------------------------

7.2 ACPI SYSTEM + DEVICE POWER STATES
-------------------------------------

System states:
--------------
S0 -> running
S3 -> suspend to RAM
S4 -> suspend to disk
S5 -> soft off

Device states:
--------------
D0 -> on
D1/D2 -> intermediate
D3 -> off

Transitions controlled by AML:

_PTS() -> preparing to sleep
_GTS() -> going to sleep
_WAK() -> waking

-------------------------------------------------------------------------------
SECTION 8: LINUX 2.6.20 BOOT FLOW (PCI VS ACPI)
-------------------------------------------------------------------------------

start_kernel()
 |
 +-- acpi_init()
 |     |
 |     +-- parse ACPI tables
 |     +-- build acpi_device tree
 |
 +-- init_IRQ()
 |
 +-- pci_init()
       |
       +-- scan PCI buses
       +-- create pci_dev
       +-- bind pci_driver

-------------------------------------------------------------------------------
SECTION 9: DEVICES THAT ARE BOTH PCI AND ACPI
-------------------------------------------------------------------------------

Example: Laptop Wi-Fi card

PCI side:
---------
- BARs
- DMA
- MSI
- Data path

ACPI side:
----------
- Power gating
- RF kill switch
- Wake events

Same physical device, two control planes.

-------------------------------------------------------------------------------
SECTION 10: DEVICES THAT ARE NEVER PCI
-------------------------------------------------------------------------------

- Embedded Controller
- Battery
- Lid switch
- Power button
- Thermal zones

Reason:
-------
They are not bus-attached devices.
They are firmware-managed logic.

-------------------------------------------------------------------------------
SECTION 11: FINAL COMPARISON TABLE (ASCII)
-------------------------------------------------------------------------------

+----------------------+---------------------------+---------------------------+
| Aspect               | PCI                       | ACPI                      |
+----------------------+---------------------------+---------------------------+
| Nature               | Hardware bus              | Firmware interface        |
| Discovery            | Probing                   | Table parsing             |
| Device identity      | Vendor / Device ID        | _HID / _CID               |
| Config mechanism     | Config space              | AML methods               |
| Performance path     | Yes                       | No                        |
| Power management     | Minimal                   | Primary purpose           |
| IRQ routing          | MSI / INTx                | GSI → IRQ                 |
| Hotplug              | Native                    | Firmware mediated         |
| Linux object         | struct pci_dev            | struct acpi_device        |
+----------------------+---------------------------+---------------------------+

-------------------------------------------------------------------------------
SECTION 12: FINAL TAKEAWAY
-------------------------------------------------------------------------------

PCI moves data
ACPI controls policy
Firmware describes
Kernel orchestrates

================================================================================
END OF FILE
================================================================================

