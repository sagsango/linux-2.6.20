Below is how I would write **arch/x86_64/kernel/setup.c** as a single IDE-style study file. This is the file I would personally spend several days on if I were learning x86-64 boot internals because it is the bridge between:

```text
head.S/startup_64
        |
        v
   setup_arch()
        |
        v
   fully initialized kernel
```

and almost every subsystem eventually starts here. The code you uploaded contains all of these paths. 

---

```text
===============================================================================
FILE: arch/x86_64/kernel/setup.c
PURPOSE: MACHINE DISCOVERY AND EARLY ARCHITECTURE INITIALIZATION
KERNEL: Linux 2.6.x x86-64
===============================================================================

===============================================================================
0. WHAT IS THIS FILE?
===============================================================================

This file answers one question:

    "What machine am I running on?"

When Linux first starts, it knows almost nothing.

It does not know:

    - how much RAM exists
    - what CPUs exist
    - whether NUMA exists
    - whether ACPI exists
    - where the kernel command line is
    - where the initrd is
    - where the BIOS tables are

setup.c discovers all of this.

Think of it as:

    machine_discovery.c

or

    hardware_inventory.c

for the entire kernel.

===============================================================================
1. BOOT FLOW BEFORE setup.c
===============================================================================

Power On
    |
    v
BIOS / UEFI
    |
    v
Bootloader (GRUB)
    |
    v
Compressed Linux Kernel
    |
    v
startup_64 (head.S)
    |
    v
start_kernel()
    |
    v
setup_arch()    <==================== YOU ARE HERE
    |
    v
paging_init()
    |
    v
sched_init()
    |
    v
rest_init()
    |
    v
PID 0
PID 1

===============================================================================
2. WHY setup_arch() EXISTS
===============================================================================

The generic kernel cannot know:

    RAM layout
    CPU topology
    APIC layout
    ACPI tables
    NUMA nodes

because these are architecture specific.

Therefore:

    start_kernel()

calls

    setup_arch()

and says:

    "Please discover the machine."

===============================================================================
3. MAJOR OBJECTS CREATED HERE
===============================================================================

boot_cpu_data

    Information about BSP CPU

------------------------------------------------------------

e820 map

    BIOS memory map

------------------------------------------------------------

cpu_data[]

    Per CPU information

------------------------------------------------------------

command_line

    Kernel boot parameters

------------------------------------------------------------

NUMA topology

    Node information

------------------------------------------------------------

Memory zones

    DMA
    DMA32
    NORMAL

------------------------------------------------------------

Resource trees

    iomem_resource
    ioport_resource

===============================================================================
4. TOP LEVEL FLOW OF setup_arch()
===============================================================================

setup_arch()

    |
    +--> Copy bootloader information
    |
    +--> Setup E820 memory map
    |
    +--> Identify CPU
    |
    +--> Parse kernel command line
    |
    +--> Discover RAM
    |
    +--> Discover BIOS regions
    |
    +--> Build direct mappings
    |
    +--> Parse DMI
    |
    +--> Parse ACPI
    |
    +--> Setup NUMA
    |
    +--> Setup bootmem allocator
    |
    +--> Reserve kernel memory
    |
    +--> Reserve initrd
    |
    +--> Setup paging
    |
    +--> Setup APIC
    |
    +--> Setup SMP
    |
    +--> Register ROM/RAM resources
    |
    +--> Finish machine discovery

===============================================================================
5. STEP 1: BOOTLOADER DATA
===============================================================================

Linux receives structures from GRUB.

Examples:

    screen_info
    edid_info
    saved_video_mode
    bootloader_type

Code:

    screen_info = SCREEN_INFO;
    edid_info = EDID_INFO;

Conceptually:

GRUB
  |
  +--> boot parameters
  |
Linux copies them
  |
  v
kernel-owned memory

===============================================================================
6. STEP 2: MEMORY MAP (E820)
===============================================================================

Most important early hardware discovery.

Function:

    setup_memory_region()

Builds:

    e820 table

Example:

    00000000-0009ffff  RAM
    000a0000-000fffff  RESERVED
    00100000-7fffffff  RAM

Linux now knows:

    usable RAM
    reserved RAM
    ROM regions
    MMIO regions

===============================================================================
7. STEP 3: EARLY CPU IDENTIFICATION
===============================================================================

Function:

    early_identify_cpu()

Uses:

    CPUID

Discovers:

    vendor
    family
    model
    stepping

Example:

    GenuineIntel

or

    AuthenticAMD

Stores result:

    boot_cpu_data

===============================================================================
8. CPU DISCOVERY FLOW
===============================================================================

CPUID
   |
   +--> Vendor
   +--> Family
   +--> Model
   +--> Stepping
   +--> Features
   |
   v
boot_cpu_data

===============================================================================
9. COMMAND LINE PARSING
===============================================================================

Kernel command line:

    console=ttyS0
    iommu=off
    reboot=t
    crashkernel=256M

Code:

    parse_early_param();

This invokes handlers from files you've already studied:

    iommu_setup()
    reboot_setup()
    setup_crashkernel()
    nopmtimer_setup()

===============================================================================
10. DISCOVER REAL RAM SIZE
===============================================================================

Code:

    end_pfn = e820_end_of_ram();

Result:

    highest RAM PFN

Example:

16 GB machine

    end_pfn ~= 4194304

Linux now knows:

    amount of physical memory

===============================================================================
11. EBDA DISCOVERY
===============================================================================

EBDA

    Extended BIOS Data Area

Ancient PC feature.

Location pointer:

    0x40E

BIOS leaves:

    segmented pointer

Linux reads it.

Reserves memory so BIOS structures are not overwritten.

===============================================================================
12. BUILD DIRECT MAP
===============================================================================

Function:

    init_memory_mapping()

Creates:

    physical -> virtual mapping

Example:

Physical:

    0x00100000

Virtual:

    ffff810000100000

This becomes the famous:

    direct mapping

Used everywhere later:

    __va()
    __pa()

===============================================================================
13. DIRECT MAP DIAGRAM
===============================================================================

Physical RAM

0x00000000
0x00100000
0x00200000
0x00300000

        |
        | direct map
        v

ffff810000000000
ffff810000100000
ffff810000200000
ffff810000300000

===============================================================================
14. DMI DISCOVERY
===============================================================================

Function:

    dmi_scan_machine()

Reads SMBIOS tables.

Discovers:

    Vendor
    Product
    BIOS Version

Examples:

    Dell
    Lenovo
    HP
    Intel

Used later by quirks.

===============================================================================
15. ACPI INITIALIZATION
===============================================================================

Function:

    acpi_boot_table_init()

Finds:

    RSDP
    RSDT/XSDT
    MADT
    SRAT

Important because:

    CPUs
    NUMA
    Interrupt routing

all come from ACPI.

===============================================================================
16. NUMA DISCOVERY
===============================================================================

Function:

    acpi_numa_init()

Reads:

    SRAT

Creates:

    NUMA nodes

Example:

Node 0:

    CPUs 0-7
    RAM 0-32GB

Node 1:

    CPUs 8-15
    RAM 32-64GB

===============================================================================
17. BOOTMEM ALLOCATOR
===============================================================================

Before buddy allocator exists:

Linux uses:

    bootmem allocator

Functions:

    init_bootmem()
    reserve_bootmem()

Bootmem manages memory during early boot.

===============================================================================
18. WHAT MEMORY GETS RESERVED?
===============================================================================

Kernel text:

    _text -> _end

------------------------------------------------------------

Page 0

    reserved forever

------------------------------------------------------------

EBDA

------------------------------------------------------------

AP trampoline

------------------------------------------------------------

Initrd

------------------------------------------------------------

Crashkernel region

===============================================================================
19. WHY PAGE ZERO IS RESERVED?
===============================================================================

Code:

    reserve_bootmem_generic(0, PAGE_SIZE);

Reason:

Many BIOSes use page zero.

Historically:

    SMP startup

    reboot paths

    firmware data

also depend on it.

===============================================================================
20. SMP TRAMPOLINE
===============================================================================

Reserved:

    SMP_TRAMPOLINE_BASE

Used when starting APs.

Flow:

BSP
 |
 +--> send startup IPI
 |
 +--> AP executes trampoline
 |
 +--> AP enters kernel

===============================================================================
21. INITRD HANDLING
===============================================================================

Code checks:

    INITRD_START
    INITRD_SIZE

Validates:

    initrd inside RAM

Then reserves it.

Result:

    initrd_start
    initrd_end

===============================================================================
22. CRASH KERNEL
===============================================================================

If:

    crashkernel=256M

then reserve memory.

Used by:

    kdump
    kexec

which you already studied.

===============================================================================
23. PAGING INITIALIZATION
===============================================================================

Function:

    paging_init()

Creates final memory management structures.

After this:

    normal page allocator possible

===============================================================================
24. APIC DISCOVERY
===============================================================================

Functions:

    acpi_boot_init()
    get_smp_config()
    init_apic_mappings()

Discovers:

    local APICs
    IOAPICs

Required for:

    interrupts
    SMP

===============================================================================
25. ROM DISCOVERY
===============================================================================

Function:

    probe_roms()

Searches:

    C0000-C7FFF

Video ROM

------------------------------------------------------------

C8000-EFFFF

Adapter ROMs

------------------------------------------------------------

F0000-FFFFF

System BIOS

===============================================================================
26. RESOURCE TREE CREATION
===============================================================================

Registers:

    RAM
    ROM
    IO ports

Examples:

    keyboard 0x60-0x6f

    PIC

    DMA controller

    timers

These become:

    iomem_resource
    ioport_resource

===============================================================================
27. CPU IDENTIFICATION DEEPER
===============================================================================

Function:

    identify_cpu()

Performs:

    vendor specific setup

        AMD
        Intel

============================================================
AMD PATH
============================================================

init_amd()

    cache discovery

    TLB discovery

    constant TSC

    multi-core detection

============================================================
INTEL PATH
============================================================

init_intel()

    cache discovery

    performance monitoring

    BTS

    PEBS

    constant TSC

    core count detection

===============================================================================
28. HYPERTHREADING DETECTION
===============================================================================

Function:

    detect_ht()

Uses CPUID.

Discovers:

    package ID

    core ID

    sibling count

Example:

Socket 0
 |
 +--> Core 0
 |     +--> Thread 0
 |     +--> Thread 1
 |
 +--> Core 1
       +--> Thread 2
       +--> Thread 3

===============================================================================
29. /proc/cpuinfo
===============================================================================

Generated by:

    show_cpuinfo()

This file is literally where:

    cat /proc/cpuinfo

gets most of its information.

Features printed:

    sse
    sse2
    nx
    ht
    vmx
    svm

etc.

===============================================================================
30. COMPLETE MACHINE DISCOVERY FLOW
===============================================================================

setup_arch()
    |
    +--> Copy bootloader data
    |
    +--> Parse E820 memory map
    |
    +--> Identify boot CPU
    |
    +--> Parse kernel command line
    |
    +--> Discover RAM
    |
    +--> Discover EBDA
    |
    +--> Build direct mappings
    |
    +--> Scan DMI
    |
    +--> Parse ACPI tables
    |
    +--> Build NUMA topology
    |
    +--> Initialize bootmem
    |
    +--> Reserve kernel memory
    |
    +--> Reserve initrd
    |
    +--> Reserve crashkernel
    |
    +--> Initialize paging
    |
    +--> Initialize APIC
    |
    +--> Discover SMP topology
    |
    +--> Discover ROMs
    |
    +--> Register RAM/IO resources
    |
    v
Machine fully discovered

===============================================================================
MENTAL MODEL
===============================================================================

head.S

    "Can I execute C code?"

setup.c

    "What hardware do I have?"

paging_init()

    "How do I manage memory?"

smpboot.c

    "How do I start other CPUs?"

init/main.c

    "How do I start the operating system?"

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/setup.c is the master machine-discovery file that
transforms a barely running CPU after startup_64 into a fully described
x86-64 system by discovering memory, CPUs, NUMA topology, ACPI tables,
bootloader parameters, ROM resources, interrupt controllers, and early
memory-management structures before the rest of the kernel is allowed
to initialize.
===============================================================================
```

