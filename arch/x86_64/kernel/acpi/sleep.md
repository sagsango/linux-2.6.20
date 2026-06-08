```text
===============================================================================
ACPI S3 SUSPEND / RESUME FLOW (Linux 2.6.20)
File: arch/x86_64/kernel/acpi.c
===============================================================================

BACKGROUND
==========

Before understanding this file, we need to understand what ACPI S3 is.

ACPI Power States
-----------------

S0 = Running
S1 = Light Sleep
S2 = Deeper Sleep
S3 = Suspend-To-RAM
S4 = Hibernate (Suspend-To-Disk)
S5 = Power Off

For Linux laptops/desktops, S3 was historically the most common
"suspend" mode.

When entering S3:

    CPU Power     -> OFF
    Cache         -> LOST
    Registers     -> LOST
    Devices       -> OFF
    RAM           -> STILL POWERED

Therefore RAM contents survive, but CPU state does not.


===============================================================================
THE PROBLEM
===============================================================================

Before Sleep:

                     Linux Kernel
                           |
                           v

+----------------------------------------------------+
| High Virtual Memory                                |
|                                                    |
| PAGE_OFFSET (0xffff880000000000 ...)               |
|                                                    |
| Kernel Code                                        |
| Kernel Stack                                       |
| Page Tables                                        |
| Drivers                                            |
+----------------------------------------------------+

CPU is executing normally.


After Wakeup:

Power Button
      |
      v
Firmware (BIOS/ACPI)
      |
      v
CPU Reset-like State
      |
      v
Real Mode / Early CPU State
      |
      X

The CPU cannot magically continue executing Linux.

Why?

Because:

    - CPU registers were lost
    - CR3 may be lost
    - Page tables not loaded
    - Long mode not active
    - Kernel virtual addresses unusable

The CPU must first be brought back into a state where Linux can run.

This is the job of the WAKEUP TRAMPOLINE.


===============================================================================
WHAT IS A WAKEUP TRAMPOLINE?
===============================================================================

A tiny piece of assembly code placed in LOW MEMORY.

Why low memory?

Because firmware can easily jump there after wakeup.

Example:

+-------------------------+
| Physical Memory         |
+-------------------------+
| 0x00000000              |
|                         |
| Wakeup Trampoline       |
| (< 1 MB)               |
|                         |
+-------------------------+

After resume:

Firmware
   |
   v
Low Memory Trampoline
   |
   v
Restore CPU State
   |
   v
Enable Paging
   |
   v
Enable Long Mode
   |
   v
Jump Back To Linux


===============================================================================
WHY IDENTITY MAPPING?
===============================================================================

During early resume we cannot assume normal kernel virtual addresses.

Linux kernel normally executes at:

    PAGE_OFFSET
        |
        v
    0xffffffff80000000 (x86_64)

Example:

Kernel Virtual Address
      |
      v
0xffffffff80123456
      |
      v
Physical Memory

But after wakeup:

    CR3 may not be restored
    Paging may not be active

Therefore early wakeup code prefers simple mappings:

    Virtual Address == Physical Address

Example:

    VA 0x1000
       |
       v
    PA 0x1000

This is called identity mapping.

This file temporarily creates such mappings.


===============================================================================
HIGH LEVEL FLOW
===============================================================================

BOOT
 |
 +--> Reserve low memory page
 |
RUNNING
 |
 +--> User presses Suspend
 |
 +--> Prepare wakeup trampoline
 |
 +--> Enter ACPI S3
 |
 +========== MACHINE OFF ==========
 |
 +--> Wake Event
 |
 +--> Firmware
 |
 +--> Wakeup Trampoline
 |
 +--> Restore CPU state
 |
 +--> Restore kernel mappings
 |
 +--> Continue Linux execution


===============================================================================
BOOT PHASE
===============================================================================

Function:
    acpi_reserve_bootmem()

Purpose:
    Reserve memory for wakeup trampoline.

Code:

    acpi_wakeup_address =
        alloc_bootmem_low(PAGE_SIZE);

Result:

+-------------------------+
| Low Memory Page         |
| Reserved For Resume     |
+-------------------------+

Saved in:

    acpi_wakeup_address


Why allocate at boot?

Because later memory may become fragmented.

We must guarantee a low-memory page exists.


===============================================================================
SUSPEND PHASE
===============================================================================

Function:
    acpi_save_state_mem()

Flow:

acpi_save_state_mem()
      |
      +--> init_low_mapping()
      |
      +--> memcpy()
      |
      +--> acpi_copy_wakeup_routine()
      |
      v
Ready For S3


===============================================================================
STEP 1 : init_low_mapping()
===============================================================================

Code:

pgd_t *slot0 =
        pgd_offset(current->mm, 0UL);

low_ptr = *slot0;

set_pgd(slot0,
        *pgd_offset(current->mm,
                    PAGE_OFFSET));

local_flush_tlb();

Purpose:

Temporarily make virtual address 0 usable.

Before:

PGD Entry 0

    PGD[0]
      |
      X

After:

PGD[0]
      |
      +--> same mapping as kernel area

Diagram:

                    Before

VA 0x00000000
      |
      X


                    After

VA 0x00000000
      |
      v
Low Physical Memory


Why?

Wakeup code will execute in low memory.

Linux must be able to access it safely.


===============================================================================
STEP 2 : Copy Wakeup Code
===============================================================================

Code:

memcpy(
    acpi_wakeup_address,
    &wakeup_start,
    &wakeup_end - &wakeup_start
);

Source:

+--------------------------+
| wakeup_start             |
|                          |
| Resume Assembly Code     |
|                          |
| wakeup_end               |
+--------------------------+

Destination:

+--------------------------+
| acpi_wakeup_address      |
|                          |
| Resume Assembly Code     |
|                          |
+--------------------------+


Why?

Firmware cannot jump into arbitrary kernel virtual memory.

The code must exist in a known physical location.


===============================================================================
STEP 3 : Patch Resume Routine
===============================================================================

Code:

acpi_copy_wakeup_routine(
        acpi_wakeup_address);

Purpose:

Fill in machine-specific details.

Possible examples:

    Restore CR3
    Restore GDT
    Restore IDT
    Restore Long Mode
    Restore Stack Pointer

Think of this as:

    "finalize the trampoline"


===============================================================================
ENTERING S3
===============================================================================

CPU State Saved
      |
      v
ACPI Firmware Called
      |
      v
Power Removed From CPU
      |
      v
RAM Remains Powered
      |
      v
Sleeping...


===============================================================================
WAKEUP SEQUENCE
===============================================================================

Power Button
      |
      v
BIOS / ACPI Firmware
      |
      v
Jump To:

    acpi_wakeup_address

      |
      v

+--------------------------+
| Wakeup Trampoline        |
+--------------------------+

      |
      +--> Reload page tables
      |
      +--> Enable paging
      |
      +--> Enable long mode
      |
      +--> Restore registers
      |
      +--> Restore stack
      |
      +--> Jump to kernel


===============================================================================
RESTORE NORMAL PAGE TABLES
===============================================================================

Function:

    acpi_restore_state_mem()

Code:

set_pgd(
    pgd_offset(current->mm, 0UL),
    low_ptr);

local_flush_tlb();

Purpose:

Remove temporary mapping created earlier.

Before:

PGD[0]
   |
   +--> Temporary Low Mapping

After:

PGD[0]
   |
   +--> Original Mapping

TLB flushed so CPU uses the new page-table entry.


===============================================================================
FULL END-TO-END FLOW
===============================================================================

                    BOOT
                      |
                      v
          acpi_reserve_bootmem()
                      |
                      v
      Reserve Low Memory Wakeup Page
                      |
                      v
                 Linux Running
                      |
                      v
              Suspend Request
                      |
                      v
          acpi_save_state_mem()
                      |
          +-----------+-----------+
          |                       |
          v                       v
  Create Low Mapping     Copy Wakeup Code
          |                       |
          +-----------+-----------+
                      |
                      v
               Enter ACPI S3
                      |
      CPU OFF / RAM STILL POWERED
                      |
                      v
                 Wake Event
                      |
                      v
             BIOS / Firmware
                      |
                      v
         Jump To Wakeup Trampoline
                      |
                      v
          Restore CPU Environment
                      |
                      v
         Restore Kernel Execution
                      |
                      v
         acpi_restore_state_mem()
                      |
                      v
             Remove Temp Mapping
                      |
                      v
             Linux Continues
===============================================================================
```

One key insight: this file is **not really about ACPI itself**. It's mostly about solving the CPU restart problem after S3. ACPI provides the sleep state; this code provides the tiny bridge ("wakeup trampoline") that gets the CPU from the firmware's primitive post-wakeup state back into a fully running 64-bit Linux kernel.

