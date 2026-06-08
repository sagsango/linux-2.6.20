============================================================
Linux x86_64 KEXEC
File: arch/x86_64/kernel/machine_kexec.c
============================================================

GOAL
------------------------------------------------------------

Boot a new Linux kernel directly from the currently
running Linux kernel.

WITHOUT:

    BIOS
    UEFI
    GRUB

Flow:

Running Linux
      |
      v
kexec -l new_kernel
      |
      v
kexec -e
      |
      v
machine_kexec()
      |
      v
Jump directly to new kernel

============================================================
1. WHAT IS KEXEC?
============================================================

Normal Boot:

Power On
   |
   v
BIOS/UEFI
   |
   v
Bootloader
   |
   v
Linux Kernel

Kexec Boot:

Linux Kernel
   |
   v
New Linux Kernel

No firmware involved.

Much faster reboot.

============================================================
2. IMPORTANT OBJECTS
============================================================

struct kimage

Contains:

    kernel image segments

    destination addresses

    entry point

    control pages

Important fields:

    image->head
    image->start
    image->control_code_page

============================================================
3. FILE OVERVIEW
============================================================

Main Functions:

machine_kexec_prepare()

machine_kexec()

machine_kexec_cleanup()

Page Table Builders:

init_pgtable()
init_level4_page()
init_level3_page()
init_level2_page()

Descriptor Helpers:

set_gdt()
set_idt()
load_segments()

Crash Kernel:

setup_crashkernel()

============================================================
4. PREPARATION PHASE
============================================================

machine_kexec_prepare()

Called during:

    kexec_load()

Flow:

    allocate temporary page tables

    build identity mapping

ASCII:

User:
    kexec -l bzImage

        |

machine_kexec_prepare()

        |

build temporary page tables

============================================================
5. PAGE TABLE HIERARCHY
============================================================

x86_64 uses:

PGD
 |
 +--> PUD
         |
         +--> PMD
                 |
                 +--> PTE

This file builds a temporary page table.

Purpose:

    identity mapping

Meaning:

    VA == PA

Example:

0x100000

maps to

0x100000

============================================================
6. init_level2_page()
============================================================

Builds PMD entries.

Uses:

    __PAGE_KERNEL_LARGE_EXEC

Creates:

    2MB large pages

ASCII:

PMD

Entry0 --> 0MB

Entry1 --> 2MB

Entry2 --> 4MB

Entry3 --> 6MB

...

Fast setup.

============================================================
7. init_level3_page()
============================================================

Builds PUD level.

Allocates PMD pages:

    kimage_alloc_control_pages()

For each PMD:

    init_level2_page()

Then:

    set_pud()

ASCII:

PUD
 |
 +--> PMD0
 |
 +--> PMD1
 |
 +--> PMD2

============================================================
8. init_level4_page()
============================================================

Builds PGD level.

Allocates PUD pages.

Calls:

    init_level3_page()

ASCII:

PGD
 |
 +--> PUD0
 |
 +--> PUD1
 |
 +--> PUD2

============================================================
9. init_pgtable()
============================================================

Top level builder.

Creates:

Identity Mapping

for:

0
    ->
end_pfn << PAGE_SHIFT

Meaning:

Map all physical RAM.

============================================================
10. WHY IDENTITY MAPPING?
============================================================

During kexec:

Current kernel page tables
are going away.

Need safe mappings.

Solution:

VA == PA

Example:

Physical:

0x00100000

Virtual:

0x00100000

No translation confusion.

============================================================
11. SEGMENT HANDLING
============================================================

Functions:

load_segments()

set_gdt()

set_idt()

============================================================
12. load_segments()
============================================================

Loads:

DS
ES
SS
FS
GS

with:

__KERNEL_DS

Important:

x86 segment registers have:

Visible Part
Invisible Cached Descriptor

ASCII:

DS
 |
 +--> selector
 |
 +--> cached descriptor

Loading segment updates cache.

============================================================
13. Why Reload Segments?
============================================================

Comment explains:

Segment registers cache descriptors.

Once loaded:

CPU no longer needs GDT.

Therefore:

load_segments()

BEFORE

destroying GDT.

============================================================
14. set_gdt()
============================================================

Uses:

lgdtq

Loads GDTR.

Later called as:

set_gdt(0,0)

Meaning:

Invalidate GDT.

============================================================
15. set_idt()
============================================================

Uses:

lidtq

Loads IDTR.

Later:

set_idt(0,0)

Meaning:

Invalidate IDT.

After this:

Interrupts/exceptions unusable.

============================================================
16. machine_kexec()
============================================================

THE MOST IMPORTANT FUNCTION

Flow:

local_irq_disable()

copy relocation code

build page_list

load segments

invalidate GDT

invalidate IDT

call relocate_kernel()

============================================================
17. Interrupt Disable
============================================================

local_irq_disable()

Reason:

No interrupts allowed.

System is shutting down.

Any interrupt:

    disaster

============================================================
18. Relocation Code
============================================================

control_page =
    image->control_code_page + PAGE_SIZE

memcpy(control_page,
       relocate_kernel,
       PAGE_SIZE)

Copies small assembly stub.

This stub performs:

    page table switch

    relocation

    jump to new kernel

============================================================
19. page_list[]
============================================================

Array passed to relocation code.

Contains:

PA_CONTROL_PAGE
PA_PGD
PA_PUD
PA_PMD
PA_PTE

and matching virtual addresses.

ASCII:

page_list

+----------------+
| control page   |
+----------------+
| pgd            |
+----------------+
| pud            |
+----------------+
| pmd            |
+----------------+
| pte            |
+----------------+

============================================================
20. GDT/IDT DESTRUCTION
============================================================

load_segments()

set_gdt(0,0)

set_idt(0,0)

After this:

No valid descriptor tables.

Reason:

Ensure relocation code
doesn't accidentally use old kernel state.

============================================================
21. Final Jump
============================================================

relocate_kernel(
        image->head,
        page_list,
        image->start
)

Arguments:

image->head

    segment list

page_list

    relocation data

image->start

    entry point

============================================================
22. ACTUAL EXECUTION FLOW
============================================================

Old Kernel
    |
    v
machine_kexec()
    |
    +--> disable interrupts
    |
    +--> copy relocation code
    |
    +--> setup page list
    |
    +--> load segments
    |
    +--> destroy GDT
    |
    +--> destroy IDT
    |
    v
relocate_kernel()
    |
    +--> install identity mappings
    |
    +--> copy image segments
    |
    +--> switch page tables
    |
    v
new kernel entry point

============================================================
23. CRASH KERNEL SUPPORT
============================================================

Parameter:

crashkernel=size@addr

Example:

crashkernel=128M@16M

Reserves:

16MB-144MB

for panic kernel.

============================================================
24. Why Crash Kernel?
============================================================

Kernel Panic
      |
      v
Boot tiny rescue kernel
      |
      v
Dump memory
      |
      v
Save vmcore

Used by:

kdump

============================================================
25. setup_crashkernel()
============================================================

Parses:

crashkernel=128M@16M

Produces:

crashk_res.start

crashk_res.end

Reserved forever.

Normal kernel won't use it.

============================================================
26. COMPLETE KEXEC FLOW
============================================================

Userspace

kexec -l newkernel
        |
        v
machine_kexec_prepare()
        |
        +--> build identity mappings

--------------------------------

kexec -e
        |
        v
machine_kexec()
        |
        +--> disable IRQs
        +--> copy relocation stub
        +--> invalidate GDT
        +--> invalidate IDT
        +--> jump to relocation code
        |
        v
relocate_kernel()
        |
        +--> copy new kernel
        +--> install new page tables
        +--> jump to entry point
        |
        v
NEW LINUX KERNEL

============================================================
27. ONE-LINE SUMMARY
============================================================

machine_kexec.c builds temporary identity-mapped page
tables, disables interrupts, tears down descriptor
tables, executes a relocation stub, and jumps directly
from the running Linux kernel into a new Linux kernel
without involving BIOS, UEFI, or a bootloader.
============================================================
