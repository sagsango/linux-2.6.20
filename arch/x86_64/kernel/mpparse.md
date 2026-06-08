============================================================
LINUX x86/x86_64 SMP DISCOVERY
File: arch/x86_64/kernel/mpparse.c
============================================================

PURPOSE
------------------------------------------------------------

This file discovers:

    CPUs
    Local APICs
    IOAPICs
    Interrupt routing

during very early boot.

Historically PCs described multiprocessor topology using:

    Intel MultiProcessor Specification (MPS)

Later systems moved to:

    ACPI MADT

This file supports both.

============================================================
BIG PICTURE
============================================================

Question:

When Linux boots on a machine with:

    8 CPUs
    2 IOAPICs
    PCI buses
    ISA devices

How does Linux know?

    CPU APIC IDs?
    IOAPIC addresses?
    IRQ routing?
    Which CPU is BSP?

Answer:

    MPS Table
or
    ACPI MADT

This file parses those tables.

============================================================
BOOT FLOW
============================================================

start_kernel()
      |
      v
setup_arch()
      |
      v
find_smp_config()
      |
      v
get_smp_config()
      |
      +--> MPS table
      |
      +--> ACPI MADT
      |
      v
Populate:

    cpu_present_map
    mp_ioapics[]
    mp_irqs[]
    mp_lapic_addr

============================================================
BACKGROUND:
INTEL MULTIPROCESSOR SPECIFICATION
============================================================

Before ACPI existed, BIOS exposed SMP information through:

    MP Floating Pointer Structure

and

    MP Configuration Table

Memory Layout:

+-----------------------+
| MP Floating Pointer   |
| "_MP_"                |
+-----------------------+
            |
            v
+-----------------------+
| MP Config Table       |
+-----------------------+

============================================================
MP FLOATING POINTER
============================================================

Signature:

    "_MP_"

Contains:

    version
    checksum
    pointer to MP config table

ASCII:

MPF
 |
 +--> mpf_physptr
         |
         v
     MP Config Table

============================================================
MP CONFIG TABLE
============================================================

Contains variable-length entries:

    Processor Entry
    Bus Entry
    IOAPIC Entry
    IRQ Source Entry
    Local Interrupt Entry

ASCII:

MPC TABLE

+------------------+
| Header           |
+------------------+
| CPU Entry        |
+------------------+
| CPU Entry        |
+------------------+
| BUS Entry        |
+------------------+
| IOAPIC Entry     |
+------------------+
| IRQ Entry        |
+------------------+

============================================================
IMPORTANT GLOBALS
============================================================

smp_found_config

    Did we find SMP configuration?

------------------------------------------------------------

num_processors

    CPUs discovered.

------------------------------------------------------------

boot_cpu_id

    BSP APIC ID.

------------------------------------------------------------

phys_cpu_present_map

    Bitmap of physical CPUs.

------------------------------------------------------------

mp_ioapics[]

    IOAPIC descriptions.

------------------------------------------------------------

mp_irqs[]

    Interrupt routing entries.

------------------------------------------------------------

mp_lapic_addr

    Physical LAPIC MMIO address.

============================================================
1. mpf_checksum()
============================================================

Purpose:

    Validate MP table checksum.

Algorithm:

    Sum every byte.

Valid table:

    sum % 256 == 0

ASCII:

byte0
 +
byte1
 +
byte2
 +
...
 =
0 mod 256

============================================================
2. MP_processor_info()
============================================================

Processes:

    Processor Entry

Structure:

    mpc_config_processor

Contains:

    APIC ID
    CPU flags
    BSP flag

============================================================
CPU DISCOVERY
============================================================

Example table:

CPU APICID=0 BSP
CPU APICID=1
CPU APICID=2
CPU APICID=3

Result:

cpu_present_map

CPU0
CPU1
CPU2
CPU3

============================================================
BOOT PROCESSOR
============================================================

CPU_BOOTPROCESSOR bit set?

Then:

    boot_cpu_id = APICID

This is BSP:

    Bootstrap Processor

The CPU that started Linux.

============================================================
CPU MAPS
============================================================

Updates:

    cpu_possible_map

    cpu_present_map

    phys_cpu_present_map

Relationship:

Logical CPU
      |
      v
Linux CPU#

Physical CPU
      |
      v
APIC ID

============================================================
3. MP_bus_info()
============================================================

Processes:

    Bus Entry

Examples:

    ISA
    PCI

============================================================
ISA BUS
============================================================

If bus type:

    ISA

then:

    set_bit(busid, mp_bus_not_pci)

Meaning:

    Legacy ISA bus

============================================================
PCI BUS
============================================================

If bus type:

    PCI

assign:

    MP bus id
           |
           v
    PCI bus number

Example:

MP bus 3
     |
     v
PCI bus 1

============================================================
4. MP_ioapic_info()
============================================================

Processes:

    IOAPIC Entry

Example:

IOAPIC ID = 2
Address   = 0xFEC00000

Stores entry in:

    mp_ioapics[]

============================================================
IOAPIC BACKGROUND
============================================================

Legacy PC:

IRQ
 |
 +--> 8259 PIC

Modern SMP:

IRQ
 |
 +--> IOAPIC
 |
 +--> Local APIC

ASCII:

Device
  |
  v
IOAPIC
  |
  v
LAPIC
  |
  v
CPU

============================================================
5. MP_intsrc_info()
============================================================

Processes:

    Interrupt Source Entry

Stores:

    IRQ routing

Example:

ISA IRQ1
      |
      v
IOAPIC ID 2
      |
      v
INTIN1

Saved in:

    mp_irqs[]

============================================================
6. MP_lintsrc_info()
============================================================

Processes:

    Local Interrupt Entry

Examples:

    ExtINT
    NMI

Used for LAPIC LINT0/LINT1 setup.

============================================================
7. smp_read_mpc()
============================================================

MOST IMPORTANT MP TABLE PARSER

Input:

    struct mp_config_table *

============================================================
VALIDATION
============================================================

Checks:

Signature:

    "PCMP"

Checksum

Version:

    1.1
    1.4

LAPIC address valid

============================================================
ENTRY WALK
============================================================

while (count < table_length)
{
    switch(type)
    {
        MP_PROCESSOR
        MP_BUS
        MP_IOAPIC
        MP_INTSRC
        MP_LINTSRC
    }
}

ASCII:

Header
 |
 +--> CPU
 |
 +--> CPU
 |
 +--> BUS
 |
 +--> IOAPIC
 |
 +--> IRQ
 |
 +--> IRQ

============================================================
RESULT OF smp_read_mpc()
============================================================

Produces:

num_processors

mp_ioapics[]

mp_irqs[]

mp_lapic_addr

cpu_present_map

============================================================
8. DEFAULT MP CONFIGURATIONS
============================================================

Old BIOSes sometimes provided:

    no MP table

Instead:

    Default Configuration Type

Example:

Type 1
Type 5

============================================================
construct_default_ISA_mptable()
============================================================

Fabricates MP tables.

Creates:

    2 CPUs

    ISA bus

    IOAPIC

    IRQ routing

without needing real MP table.

============================================================
DEFAULT TOPOLOGY
============================================================

Generated:

CPU0
CPU1

Bus0 = ISA

IOAPIC ID=2

LAPIC = default address

============================================================
9. ELCR SUPPORT
============================================================

ELCR

Edge/Level Control Register

Ports:

    0x4d0
    0x4d1

Used to determine:

    edge-triggered IRQ

or

    level-triggered IRQ

============================================================
WHY ELCR?
============================================================

Broken BIOS:

    no IRQ routing entries

Kernel guesses routing using ELCR.

Example:

IRQ11 level triggered

Probably PCI interrupt.

============================================================
10. find_smp_config()
============================================================

Searches memory for:

    "_MP_"

signature.

============================================================
SEARCH REGIONS
============================================================

Region 1:

    first 1KB RAM

------------------------------------------------------------

Region 2:

    top 1KB of base memory

------------------------------------------------------------

Region 3:

    BIOS ROM

    0xF0000-0xFFFFF

------------------------------------------------------------

Region 4:

    EBDA

============================================================
EBDA
============================================================

Extended BIOS Data Area.

Pointer stored at:

    0x40E

Real mode:

    segment address

converted to:

    physical address

Then scanned.

============================================================
smp_scan_config()
============================================================

Searches memory for:

    "_MP_"

Checks:

    length == 1

    checksum valid

    version = 1 or 4

If found:

    reserve_bootmem()

    mpf_found = mpf

============================================================
11. get_smp_config()
============================================================

Build final SMP configuration.

Decision:

============================================================

ACPI LAPIC + ACPI IOAPIC available?

YES

    Use ACPI

NO

    Use MP tables

============================================================

ACPI path:

    MADT

MPS path:

    MP tables

============================================================
ACPI BACKGROUND
============================================================

Modern systems use:

    ACPI MADT

MADT = Multiple APIC Description Table

Contains:

    LAPIC entries
    IOAPIC entries
    IRQ overrides

Much richer than MPS.

============================================================
12. ACPI LAPIC REGISTRATION
============================================================

mp_register_lapic()

Creates synthetic:

    mpc_config_processor

Then reuses:

    MP_processor_info()

Very clever:

ACPI data
      |
      v
fake MP entry
      |
      v
existing parser

============================================================
13. ACPI IOAPIC REGISTRATION
============================================================

mp_register_ioapic()

Input:

    APIC ID
    Address
    GSI Base

Creates:

    mp_ioapics[]

============================================================
GSI
============================================================

Global System Interrupt

Example:

IOAPIC0

GSI 0-23

IOAPIC1

GSI 24-47

============================================================
IOAPIC ROUTING TABLE
============================================================

mp_ioapic_routing[]

Maps:

GSI
 |
 +--> IOAPIC
 |
 +--> PIN

Example:

GSI 35
    |
    v
IOAPIC1
PIN11

============================================================
14. IRQ OVERRIDES
============================================================

ACPI MADT contains:

Interrupt Source Overrides

Example:

Legacy IRQ0

actually routed to

GSI2

Function:

    mp_override_legacy_irq()

Creates corresponding MP-style routing entry.

============================================================
15. ACPI LEGACY IRQ FABRICATION
============================================================

mp_config_acpi_legacy_irqs()

Creates ISA IRQ mappings:

IRQ0
IRQ1
IRQ2
...
IRQ15

unless already overridden.

============================================================
16. PCI IRQ ROUTING
============================================================

mp_register_gsi()

Used by ACPI PCI routing.

Flow:

PCI Device
      |
      v
PRT entry
      |
      v
GSI
      |
      v
IOAPIC
      |
      v
IOAPIC Pin

Programs:

    io_apic_set_pci_routing()

============================================================
PIN PROGRAMMING PROTECTION
============================================================

Same IOAPIC pin may appear multiple times.

Kernel tracks:

    pin_programmed[]

Avoids reprogramming same pin.

============================================================
EXAMPLE BOOT FLOW (MODERN SYSTEM)
============================================================

BIOS/UEFI
      |
      v
ACPI MADT
      |
      +--> LAPIC CPU0
      +--> LAPIC CPU1
      +--> LAPIC CPU2
      +--> LAPIC CPU3
      |
      +--> IOAPIC0
      |
      +--> IRQ overrides
      |
      v
mp_register_lapic()
mp_register_ioapic()
mp_override_legacy_irq()
      |
      v
Linux SMP topology ready

============================================================
EXAMPLE BOOT FLOW (OLD SYSTEM)
============================================================

BIOS
  |
  v
_MP_
  |
  v
MP Config Table
  |
  +--> Processor entries
  +--> Bus entries
  +--> IOAPIC entries
  +--> IRQ entries
  |
  v
smp_read_mpc()
  |
  v
Linux SMP topology ready

============================================================
FINAL DATA STRUCTURES PRODUCED
============================================================

cpu_present_map

Which CPUs exist.

------------------------------------------------------------

cpu_possible_map

Which CPUs may exist.

------------------------------------------------------------

bios_cpu_apicid[]

Logical CPU -> APIC ID

------------------------------------------------------------

mp_ioapics[]

All IOAPICs

------------------------------------------------------------

mp_irqs[]

IRQ routing entries

------------------------------------------------------------

mp_lapic_addr

LAPIC MMIO address

============================================================
WHY THIS FILE MATTERS
============================================================

Before Linux can:

    start APs
    send IPIs
    program IOAPICs
    route interrupts
    enable SMP

it must know:

    CPUs
    APIC IDs
    IOAPICs
    interrupt topology

This file discovers all of that.

============================================================
MENTAL MODEL
============================================================

Think of this file as:

    "Hardware Topology Discovery"

for x86 SMP.

Without it Linux would not know:

    how many CPUs exist
    where LAPIC is
    where IOAPICs are
    how IRQs are routed

Everything in later SMP/APIC code depends on data built here.

============================================================
ONE-LINE SUMMARY
============================================================

mpparse.c discovers and parses x86 SMP topology from Intel
MP tables or ACPI MADT tables, building Linux's internal
CPU, LAPIC, IOAPIC, bus, and interrupt-routing structures
used by the rest of the SMP and interrupt subsystem.
============================================================
