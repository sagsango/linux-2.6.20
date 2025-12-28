# PCI vs PCIe
================================================================================
                   PCI / PCIe — FROM ZERO TO KERNEL-LEVEL DEPTH
================================================================================

TABLE OF CONTENTS
-----------------
  0. Why PCI Exists
  1. What Is a Bus?
  2. Pre-PCI Era (ISA, EISA)
  3. PCI Fundamentals
  4. PCI Electrical + Logical Model
  5. PCI Address Spaces
  6. PCI Configuration Space (256B)
  7. PCI Enumeration (Firmware → OS)
  8. BARs (Base Address Registers)
  9. PCI Interrupts (INTx)
 10. DMA in PCI
 11. PCI Bridges and Hierarchy
 12. PCI-X (Why It Failed)
 13. Why PCIe Was Needed
 14. PCIe Fundamentals (Packet-Based)
 15. PCIe Topology (Root / Switch / Endpoint)
 16. PCIe Configuration Space (4KB)
 17. PCIe Transactions (TLPs)
 18. PCIe Address Translation
 19. MSI / MSI-X Interrupts
 20. PCIe Power Management
 21. Firmware Role (BIOS / UEFI / ACPI)
 22. Linux PCI Subsystem Overview
 23. Full End-to-End Example (Boot → Driver)
================================================================================


================================================================================
0. WHY PCI EXISTS
================================================================================

PROBLEM BEFORE PCI:
-------------------
- Every device had:
  * Fixed I/O port
  * Fixed IRQ
  * Fixed DMA channel
- Manual jumpers on cards
- Conflicts everywhere

GOAL OF PCI:
------------
- Plug-and-play
- Auto resource allocation
- OS-controlled enumeration
- Bus mastering (DMA)
- Scalable device model


================================================================================
1. WHAT IS A BUS?
================================================================================

A BUS IS:
---------
- Shared communication medium
- Multiple devices attached
- Arbitration rules define who talks

THREE CORE QUESTIONS:
---------------------
1. How does CPU talk to device?
2. How does device talk to memory?
3. How do multiple devices coexist?


================================================================================
2. PRE-PCI ERA (ISA, EISA)
================================================================================

ISA BUS (VERY OLD):
------------------
- 8/16-bit
- No discovery
- Fixed IRQ / IO / DMA
- CPU-centric

ISA MODEL:
----------
CPU ---- I/O PORTS ---- DEVICE

NO:
- Enumeration
- Bus mastering
- Protection


================================================================================
3. PCI FUNDAMENTALS
================================================================================

PCI = Peripheral Component Interconnect

CORE IDEAS:
-----------
- Memory-mapped devices
- Configuration space
- Centralized enumeration
- Bus mastering DMA
- Hierarchical buses

PCI IS:
-------
- Parallel shared bus
- Clocked (33MHz / 66MHz)
- Address + data multiplexed


================================================================================
4. PCI ELECTRICAL + LOGICAL MODEL
================================================================================

ELECTRICAL:
-----------
- Shared parallel wires
- All devices see all transactions
- Arbitration via REQ#/GNT#

LOGICAL VIEW:
-------------
                +---------+
                |   CPU   |
                +----+----+
                     |
              +------+------+
              |   PCI HOST  |
              |  (Northbridge)
              +------+------+
                     |
     ---------------------------------
     |           |           |
  +--+--+     +--+--+     +--+--+
  | NIC |     | GPU |     | USB |
  +-----+     +-----+     +-----+

ALL DEVICES SHARE SAME BUS


================================================================================
5. PCI ADDRESS SPACES
================================================================================

PCI DEFINES 3 SPACES:
---------------------
1. Configuration Space
2. Memory Space
3. I/O Space

CONFIG SPACE:
-------------
- Used for enumeration
- Accessed via special cycles

MEMORY SPACE:
-------------
- MMIO
- Device registers mapped into CPU address space

I/O SPACE:
----------
- Legacy x86 IN/OUT
- Mostly obsolete


================================================================================
6. PCI CONFIGURATION SPACE (256 BYTES)
================================================================================

EACH PCI FUNCTION HAS 256 BYTES

OFFSET MAP:
-----------
00h  Vendor ID (16)
02h  Device ID (16)
04h  Command
06h  Status
08h  Revision ID
09h  Class Code (ProgIF / Subclass / Base)
0Ch  Cache Line Size
0Dh  Latency Timer
0Eh  Header Type
0Fh  BIST

10h-27h  BAR0 - BAR5
28h-2Bh  Cardbus CIS
2Ch-2Dh  Subsystem Vendor ID
2Eh-2Fh  Subsystem ID
30h-33h  Expansion ROM BAR
3Ch-3Dh  Interrupt Line
3Eh-3Fh  Interrupt Pin

KEY IDEA:
---------
OS reads Vendor ID
If 0xFFFF → no device


================================================================================
7. PCI ENUMERATION (FIRMWARE → OS)
================================================================================

ENUMERATION MODEL:
------------------
Bus 0
  |
  +-- Device 0..31
         |
         +-- Function 0..7

FIRMWARE STEPS:
---------------
FOR bus = 0..255
  FOR device = 0..31
    FOR function = 0..7
      read vendor_id
      if exists:
         assign resources
         setup BARs

CONFIG ACCESS MECHANISM:
------------------------
Legacy x86:
- IO port 0xCF8 (address)
- IO port 0xCFC (data)


================================================================================
8. BARs (BASE ADDRESS REGISTERS)
================================================================================

WHAT IS A BAR?
--------------
- Tells where device registers live
- Either MMIO or IO

BAR TYPES:
----------
- 32-bit Memory
- 64-bit Memory
- I/O

BAR PROBING:
------------
OS writes all 1s to BAR
Reads back size mask
Allocates region
Writes final address

BAR EXAMPLE:
------------
BAR0 = 0xFEBF0000

CPU ACCESS:
-----------
*(volatile u32 *)(0xFEBF0000 + offset)


================================================================================
9. PCI INTERRUPTS (INTx)
================================================================================

LEGACY INTERRUPTS:
------------------
INTA#, INTB#, INTC#, INTD#

SHARED:
-------
- Multiple devices share same IRQ
- Level-triggered

FLOW:
-----
DEVICE asserts INTA#
  ↓
PCI Host routes to IOAPIC
  ↓
CPU interrupt
  ↓
Driver checks status register


================================================================================
10. DMA IN PCI
================================================================================

BUS MASTERING:
--------------
Device can initiate memory access

FLOW:
-----
Driver programs DMA address
Device issues PCI transaction
Memory controller handles write

NO CPU INVOLVEMENT

LIMITATION:
-----------
- Device sees physical memory
- Needs IOMMU for isolation


================================================================================
11. PCI BRIDGES AND HIERARCHY
================================================================================

WHY BRIDGES?
------------
- Too many devices
- Electrical limits

PCI-PCI BRIDGE:
---------------
Creates new bus

TREE STRUCTURE:
---------------
Bus 0
 |
 +-- Bridge → Bus 1
 |              |
 |           Devices
 |
 +-- Bridge → Bus 2

EACH BUS:
---------
- Own config space
- Own address windows


================================================================================
12. PCI-X (WHY IT FAILED)
================================================================================

PCI-X:
------
- Faster parallel PCI
- 64-bit
- 133 MHz

PROBLEMS:
---------
- Still shared bus
- Signal integrity
- Poor scalability

RESULT:
--------
Dead end


================================================================================
13. WHY PCIe WAS NEEDED
================================================================================

PROBLEMS WITH PCI:
------------------
- Shared bus
- Clock skew
- Electrical limits
- Interrupt sharing
- Poor scaling

PCIe SOLUTION:
--------------
- Serial
- Point-to-point
- Packet-based
- Switch fabric


================================================================================
14. PCIe FUNDAMENTALS
================================================================================

PCIe IS NOT A BUS
-----------------
It is a NETWORK

LINK:
-----
- Lanes (x1, x4, x8, x16)
- Full duplex
- Differential pairs

EACH DEVICE HAS DEDICATED LINK


================================================================================
15. PCIe TOPOLOGY
================================================================================

ROOT COMPLEX:
-------------
Owned by CPU

TOPOLOGY:
---------
CPU
 |
Root Complex
 |
 +-- Switch
 |     |
 |   Endpoints
 |
 +-- Endpoint

NO SHARED WIRES


================================================================================
16. PCIe CONFIGURATION SPACE (4 KB)
================================================================================

FIRST 256 BYTES:
---------------
PCI-compatible

EXTENDED CONFIG:
----------------
Capabilities
MSI
MSI-X
PCIe caps
AER
SR-IOV

ACCESS:
-------
MMIO based (ECAM)


================================================================================
17. PCIe TRANSACTIONS (TLPs)
================================================================================

TRANSACTION LAYER PACKET (TLP):
-------------------------------
- Memory Read
- Memory Write
- Config Read
- Config Write
- Completion

FLOW:
-----
CPU issues load
↓
Root Complex generates TLP
↓
Switch routes
↓
Endpoint processes
↓
Completion returned


================================================================================
18. PCIe ADDRESS TRANSLATION
================================================================================

ADDRESS TYPES:
--------------
- Memory
- Config
- Message

NO I/O SPACE (mostly)

IOMMU ROLE:
-----------
Device VA → Host PA
Protection
Virtualization


================================================================================
19. MSI / MSI-X INTERRUPTS
================================================================================

NO PHYSICAL WIRES

INTERRUPT = MEMORY WRITE

FLOW:
-----
Device writes to MSI address
↓
LAPIC receives
↓
CPU interrupt

MSI-X:
------
- Multiple vectors
- Per-queue interrupts
- Essential for high-performance


================================================================================
20. PCIe POWER MANAGEMENT
================================================================================

LINK STATES:
------------
L0  Active
L1  Low power
L2  Sleep
L3  Off

DEVICE STATES:
--------------
D0..D3

CONTROLLED VIA:
---------------
PCIe config space
ACPI


================================================================================
21. FIRMWARE ROLE (BIOS / UEFI / ACPI)
================================================================================

FIRMWARE DOES:
--------------
- Early enumeration
- BAR sizing
- IRQ routing
- ACPI tables (_PRT)

OS MAY:
-------
- Re-enumerate
- Reassign resources


================================================================================
22. LINUX PCI SUBSYSTEM OVERVIEW
================================================================================

CORE FILES:
-----------
drivers/pci/
include/linux/pci.h

FLOW:
-----
pci_init()
  |
pci_scan_root_bus()
  |
pci_scan_child_bus()
  |
pci_bus_add_device()
  |
driver match
  |
probe()


================================================================================
23. FULL END-TO-END EXAMPLE
================================================================================

BOOT:
-----
UEFI enumerates PCIe
Builds ACPI tables

KERNEL:
-------
pci_init()
scan buses
detect device
match driver

DRIVER:
-------
probe()
read BAR
ioremap()
request_irq()
enable DMA

RUNTIME:
--------
MMIO access
DMA
MSI interrupts

================================================================================
END OF FILE
================================================================================
================================================================================
                   PCI / PCIe — FROM ZERO TO KERNEL-LEVEL DEPTH
================================================================================

TABLE OF CONTENTS
-----------------
  0. Why PCI Exists
  1. What Is a Bus?
  2. Pre-PCI Era (ISA, EISA)
  3. PCI Fundamentals
  4. PCI Electrical + Logical Model
  5. PCI Address Spaces
  6. PCI Configuration Space (256B)
  7. PCI Enumeration (Firmware → OS)
  8. BARs (Base Address Registers)
  9. PCI Interrupts (INTx)
 10. DMA in PCI
 11. PCI Bridges and Hierarchy
 12. PCI-X (Why It Failed)
 13. Why PCIe Was Needed
 14. PCIe Fundamentals (Packet-Based)
 15. PCIe Topology (Root / Switch / Endpoint)
 16. PCIe Configuration Space (4KB)
 17. PCIe Transactions (TLPs)
 18. PCIe Address Translation
 19. MSI / MSI-X Interrupts
 20. PCIe Power Management
 21. Firmware Role (BIOS / UEFI / ACPI)
 22. Linux PCI Subsystem Overview
 23. Full End-to-End Example (Boot → Driver)
================================================================================


================================================================================
0. WHY PCI EXISTS
================================================================================

PROBLEM BEFORE PCI:
-------------------
- Every device had:
  * Fixed I/O port
  * Fixed IRQ
  * Fixed DMA channel
- Manual jumpers on cards
- Conflicts everywhere

GOAL OF PCI:
------------
- Plug-and-play
- Auto resource allocation
- OS-controlled enumeration
- Bus mastering (DMA)
- Scalable device model


================================================================================
1. WHAT IS A BUS?
================================================================================

A BUS IS:
---------
- Shared communication medium
- Multiple devices attached
- Arbitration rules define who talks

THREE CORE QUESTIONS:
---------------------
1. How does CPU talk to device?
2. How does device talk to memory?
3. How do multiple devices coexist?


================================================================================
2. PRE-PCI ERA (ISA, EISA)
================================================================================

ISA BUS (VERY OLD):
------------------
- 8/16-bit
- No discovery
- Fixed IRQ / IO / DMA
- CPU-centric

ISA MODEL:
----------
CPU ---- I/O PORTS ---- DEVICE

NO:
- Enumeration
- Bus mastering
- Protection


================================================================================
3. PCI FUNDAMENTALS
================================================================================

PCI = Peripheral Component Interconnect

CORE IDEAS:
-----------
- Memory-mapped devices
- Configuration space
- Centralized enumeration
- Bus mastering DMA
- Hierarchical buses

PCI IS:
-------
- Parallel shared bus
- Clocked (33MHz / 66MHz)
- Address + data multiplexed


================================================================================
4. PCI ELECTRICAL + LOGICAL MODEL
================================================================================

ELECTRICAL:
-----------
- Shared parallel wires
- All devices see all transactions
- Arbitration via REQ#/GNT#

LOGICAL VIEW:
-------------
                +---------+
                |   CPU   |
                +----+----+
                     |
              +------+------+
              |   PCI HOST  |
              |  (Northbridge)
              +------+------+
                     |
     ---------------------------------
     |           |           |
  +--+--+     +--+--+     +--+--+
  | NIC |     | GPU |     | USB |
  +-----+     +-----+     +-----+

ALL DEVICES SHARE SAME BUS


================================================================================
5. PCI ADDRESS SPACES
================================================================================

PCI DEFINES 3 SPACES:
---------------------
1. Configuration Space
2. Memory Space
3. I/O Space

CONFIG SPACE:
-------------
- Used for enumeration
- Accessed via special cycles

MEMORY SPACE:
-------------
- MMIO
- Device registers mapped into CPU address space

I/O SPACE:
----------
- Legacy x86 IN/OUT
- Mostly obsolete


================================================================================
6. PCI CONFIGURATION SPACE (256 BYTES)
================================================================================

EACH PCI FUNCTION HAS 256 BYTES

OFFSET MAP:
-----------
00h  Vendor ID (16)
02h  Device ID (16)
04h  Command
06h  Status
08h  Revision ID
09h  Class Code (ProgIF / Subclass / Base)
0Ch  Cache Line Size
0Dh  Latency Timer
0Eh  Header Type
0Fh  BIST

10h-27h  BAR0 - BAR5
28h-2Bh  Cardbus CIS
2Ch-2Dh  Subsystem Vendor ID
2Eh-2Fh  Subsystem ID
30h-33h  Expansion ROM BAR
3Ch-3Dh  Interrupt Line
3Eh-3Fh  Interrupt Pin

KEY IDEA:
---------
OS reads Vendor ID
If 0xFFFF → no device


================================================================================
7. PCI ENUMERATION (FIRMWARE → OS)
================================================================================

ENUMERATION MODEL:
------------------
Bus 0
  |
  +-- Device 0..31
         |
         +-- Function 0..7

FIRMWARE STEPS:
---------------
FOR bus = 0..255
  FOR device = 0..31
    FOR function = 0..7
      read vendor_id
      if exists:
         assign resources
         setup BARs

CONFIG ACCESS MECHANISM:
------------------------
Legacy x86:
- IO port 0xCF8 (address)
- IO port 0xCFC (data)


================================================================================
8. BARs (BASE ADDRESS REGISTERS)
================================================================================

WHAT IS A BAR?
--------------
- Tells where device registers live
- Either MMIO or IO

BAR TYPES:
----------
- 32-bit Memory
- 64-bit Memory
- I/O

BAR PROBING:
------------
OS writes all 1s to BAR
Reads back size mask
Allocates region
Writes final address

BAR EXAMPLE:
------------
BAR0 = 0xFEBF0000

CPU ACCESS:
-----------
*(volatile u32 *)(0xFEBF0000 + offset)


================================================================================
9. PCI INTERRUPTS (INTx)
================================================================================

LEGACY INTERRUPTS:
------------------
INTA#, INTB#, INTC#, INTD#

SHARED:
-------
- Multiple devices share same IRQ
- Level-triggered

FLOW:
-----
DEVICE asserts INTA#
  ↓
PCI Host routes to IOAPIC
  ↓
CPU interrupt
  ↓
Driver checks status register


================================================================================
10. DMA IN PCI
================================================================================

BUS MASTERING:
--------------
Device can initiate memory access

FLOW:
-----
Driver programs DMA address
Device issues PCI transaction
Memory controller handles write

NO CPU INVOLVEMENT

LIMITATION:
-----------
- Device sees physical memory
- Needs IOMMU for isolation


================================================================================
11. PCI BRIDGES AND HIERARCHY
================================================================================

WHY BRIDGES?
------------
- Too many devices
- Electrical limits

PCI-PCI BRIDGE:
---------------
Creates new bus

TREE STRUCTURE:
---------------
Bus 0
 |
 +-- Bridge → Bus 1
 |              |
 |           Devices
 |
 +-- Bridge → Bus 2

EACH BUS:
---------
- Own config space
- Own address windows


================================================================================
12. PCI-X (WHY IT FAILED)
================================================================================

PCI-X:
------
- Faster parallel PCI
- 64-bit
- 133 MHz

PROBLEMS:
---------
- Still shared bus
- Signal integrity
- Poor scalability

RESULT:
--------
Dead end


================================================================================
13. WHY PCIe WAS NEEDED
================================================================================

PROBLEMS WITH PCI:
------------------
- Shared bus
- Clock skew
- Electrical limits
- Interrupt sharing
- Poor scaling

PCIe SOLUTION:
--------------
- Serial
- Point-to-point
- Packet-based
- Switch fabric


================================================================================
14. PCIe FUNDAMENTALS
================================================================================

PCIe IS NOT A BUS
-----------------
It is a NETWORK

LINK:
-----
- Lanes (x1, x4, x8, x16)
- Full duplex
- Differential pairs

EACH DEVICE HAS DEDICATED LINK


================================================================================
15. PCIe TOPOLOGY
================================================================================

ROOT COMPLEX:
-------------
Owned by CPU

TOPOLOGY:
---------
CPU
 |
Root Complex
 |
 +-- Switch
 |     |
 |   Endpoints
 |
 +-- Endpoint

NO SHARED WIRES


================================================================================
16. PCIe CONFIGURATION SPACE (4 KB)
================================================================================

FIRST 256 BYTES:
---------------
PCI-compatible

EXTENDED CONFIG:
----------------
Capabilities
MSI
MSI-X
PCIe caps
AER
SR-IOV

ACCESS:
-------
MMIO based (ECAM)


================================================================================
17. PCIe TRANSACTIONS (TLPs)
================================================================================

TRANSACTION LAYER PACKET (TLP):
-------------------------------
- Memory Read
- Memory Write
- Config Read
- Config Write
- Completion

FLOW:
-----
CPU issues load
↓
Root Complex generates TLP
↓
Switch routes
↓
Endpoint processes
↓
Completion returned


================================================================================
18. PCIe ADDRESS TRANSLATION
================================================================================

ADDRESS TYPES:
--------------
- Memory
- Config
- Message

NO I/O SPACE (mostly)

IOMMU ROLE:
-----------
Device VA → Host PA
Protection
Virtualization


================================================================================
19. MSI / MSI-X INTERRUPTS
================================================================================

NO PHYSICAL WIRES

INTERRUPT = MEMORY WRITE

FLOW:
-----
Device writes to MSI address
↓
LAPIC receives
↓
CPU interrupt

MSI-X:
------
- Multiple vectors
- Per-queue interrupts
- Essential for high-performance


================================================================================
20. PCIe POWER MANAGEMENT
================================================================================

LINK STATES:
------------
L0  Active
L1  Low power
L2  Sleep
L3  Off

DEVICE STATES:
--------------
D0..D3

CONTROLLED VIA:
---------------
PCIe config space
ACPI


================================================================================
21. FIRMWARE ROLE (BIOS / UEFI / ACPI)
================================================================================

FIRMWARE DOES:
--------------
- Early enumeration
- BAR sizing
- IRQ routing
- ACPI tables (_PRT)

OS MAY:
-------
- Re-enumerate
- Reassign resources


================================================================================
22. LINUX PCI SUBSYSTEM OVERVIEW
================================================================================

CORE FILES:
-----------
drivers/pci/
include/linux/pci.h

FLOW:
-----
pci_init()
  |
pci_scan_root_bus()
  |
pci_scan_child_bus()
  |
pci_bus_add_device()
  |
driver match
  |
probe()


================================================================================
23. FULL END-TO-END EXAMPLE
================================================================================

BOOT:
-----
UEFI enumerates PCIe
Builds ACPI tables

KERNEL:
-------
pci_init()
scan buses
detect device
match driver

DRIVER:
-------
probe()
read BAR
ioremap()
request_irq()
enable DMA

RUNTIME:
--------
MMIO access
DMA
MSI interrupts

================================================================================
END OF FILE
================================================================================

