Excellent. This file is the missing half of the story. The C file (`acpi.c`) only prepares the trampoline. This assembly file is the actual **CPU resurrection code** that runs after S3 wakeup. 

I would add the following section to your notes:

```text
===============================================================================
HOW CPU RETURNS FROM S3 TO 64-BIT LINUX
File: wakeup.S
===============================================================================

BACKGROUND
==========

Before Suspend:

    Linux Kernel
         |
         v
    64-bit Long Mode
         |
         +--> Paging ON
         +--> CR3 Loaded
         +--> GDT Loaded
         +--> IDT Loaded
         +--> Kernel Stack Exists

Everything is normal.


After Resume:

    Power Button
         |
         v
    BIOS/ACPI Firmware
         |
         v
    CPU starts almost from scratch

CPU State:

    Real Mode
    16-bit
    Paging OFF
    Long Mode OFF
    Kernel mappings unavailable

Linux cannot execute normal kernel code yet.

Therefore we need a bridge:

    Real Mode
         |
         v
    Protected Mode (32-bit)
         |
         v
    Compatibility Mode
         |
         v
    Long Mode (64-bit)
         |
         v
    Restore Registers
         |
         v
    Continue Linux


===============================================================================
WHO COPIES THIS CODE?
===============================================================================

acpi_save_state_mem()
    |
    +--> memcpy()
            |
            v
       wakeup_start
            :
       wakeup_end

Copied to:

    acpi_wakeup_address

Low Memory:

+-------------------------+
| Wakeup Trampoline       |
+-------------------------+

Firmware jumps here after S3 wakeup.


===============================================================================
PHASE 1 : REAL MODE (16-bit)
===============================================================================

Entry Point:

ENTRY(wakeup_start)

CPU State:

    Real Mode
    16-bit

Code:

    cli
    cld

Disable interrupts.

Setup segments:

    movw %cs,%ax
    movw %ax,%ds
    movw %ax,%ss

Setup temporary stack:

    mov $(wakeup_stack),%sp


Flow:

Firmware
    |
    v
wakeup_start
    |
    +--> Setup DS
    |
    +--> Setup SS
    |
    +--> Setup Stack
    |
    +--> Disable Interrupts


===============================================================================
MAGIC NUMBER CHECK
===============================================================================

real_magic
    |
    v
0x12345678 ?

If NO:

    bogus_real_magic:
        loop forever

Purpose:

Verify trampoline was copied correctly.


===============================================================================
OPTIONAL VIDEO RESTORE
===============================================================================

if (acpi_video_flags & 1)

    lcall $0xc000,$3

Restore video BIOS state.

Old laptop workaround.


===============================================================================
LOAD GDT + ENTER PROTECTED MODE
===============================================================================

Current:

    Real Mode

Need:

    Protected Mode

Load GDT:

    lgdt

Enable PE bit:

    movl $1,%eax
    lmsw %ax

Now:

    CR0.PE = 1

CPU enters Protected Mode.

Jump:

    jmp wakeup_32


Flow:

Real Mode
     |
     v
Load GDT
     |
     v
CR0.PE=1
     |
     v
Protected Mode


===============================================================================
PHASE 2 : PROTECTED MODE (32-bit)
===============================================================================

Label:

    wakeup_32

CPU State:

    32-bit Protected Mode
    Paging OFF

Setup segments:

    DS
    ES
    FS
    GS
    SS

Load saved stack.

Check:

    saved_magic

Verify suspend context exists.


===============================================================================
CHECK CPU SUPPORTS LONG MODE
===============================================================================

cpuid

Need:

    EDX bit 29

If absent:

    bogus_cpu:
        infinite loop


Flow:

CPUID
   |
   +--> Long Mode Supported?
              |
      +-------+------+
      |              |
      No            Yes
      |              |
      Loop       Continue


===============================================================================
ENABLE PAE
===============================================================================

CR4.PAE = 1

Required before Long Mode.

Code:

    btsl $5,%eax
    mov %eax,%cr4


Result:

    CR4.PAE = 1


===============================================================================
LOAD PML4
===============================================================================

mov wakeup_level4_pgt,%cr3

This is the first 64-bit page table.

Now CPU knows where paging structures are.


Flow:

CR3
 |
 v
PML4
 |
 +--> PDP
 |
 +--> PD
 |
 +--> PT


===============================================================================
ENABLE LONG MODE
===============================================================================

MSR_EFER.LME = 1

Code:

    rdmsr
    btsl $_EFER_LME,%eax
    wrmsr


Now:

    Long Mode READY

but NOT active yet.


===============================================================================
TURN ON PAGING
===============================================================================

CR0.PG = 1

mov %eax,%cr0

At this exact moment:

    CR4.PAE=1
    CR3=PML4
    EFER.LME=1
    CR0.PG=1

CPU transitions:

    Protected Mode
          |
          v
    Compatibility Mode

Still executing 32-bit instructions.


===============================================================================
COMPATIBILITY MODE
===============================================================================

Label:

    reach_compatibility_mode

State:

    Long Mode Active
    32-bit Code Segment

This is called:

    Compatibility Mode

Need one final jump.


===============================================================================
LOAD 64-BIT GDT
===============================================================================

lgdt pGDT32

Contains:

    __KERNEL_CS

with

    L = 1

Meaning:

    64-bit code segment


===============================================================================
FAR JUMP TO 64-BIT MODE
===============================================================================

ljmp wakeup_long64

CPU State Transition:

Real Mode
     |
     v
Protected Mode
     |
     v
Compatibility Mode
     |
     v
Long Mode


This is the most important jump in the file.


===============================================================================
PHASE 3 : TRUE 64-BIT MODE
===============================================================================

Label:

    wakeup_long64

Now CPU is fully 64-bit.

Setup:

    lgdt cpu_gdt_descr

Load real kernel GDT.

Restore:

    SS
    DS
    ES
    FS
    GS


Restore saved registers:

    RSP
    RBP
    RBX
    RSI
    RDI


Flow:

64-bit Entry
      |
      +--> Restore Stack
      |
      +--> Restore Registers
      |
      +--> Restore Segments


===============================================================================
RETURN TO ORIGINAL KERNEL CODE
===============================================================================

saved_eip
      |
      v
Kernel Resume Address

Code:

    movq saved_eip,%rax
    jmp *%rax

This returns to:

    do_suspend_lowlevel()

which resumes execution immediately after:

    acpi_enter_sleep_state()


===============================================================================
COMPLETE RESUME PATH
===============================================================================

S3 Wake Event
      |
      v
BIOS
      |
      v
wakeup_start
      |
      v
16-bit Real Mode
      |
      v
Load GDT
      |
      v
Protected Mode
      |
      v
Enable PAE
      |
      v
Load CR3 (PML4)
      |
      v
Enable EFER.LME
      |
      v
Enable Paging
      |
      v
Compatibility Mode
      |
      v
Far Jump
      |
      v
64-bit Long Mode
      |
      v
Restore Registers
      |
      v
Jump To saved_eip
      |
      v
Linux Continues Running

===============================================================================

KEY INSIGHT
===========

This entire file exists for one reason:

    CPU wakes in 16-bit Real Mode
           |
           v
    Linux was suspended in 64-bit Long Mode

The wakeup trampoline is the bridge that reconstructs:

    CR0
    CR3
    CR4
    EFER
    GDT
    IDT
    Stack
    CPU Registers

and safely transitions:

    Real Mode
        ->
    Protected Mode
        ->
    Compatibility Mode
        ->
    Long Mode
        ->
    Original Linux Kernel
```

This is one of the best examples in the kernel of the complete x86 boot path in reverse: instead of going **BIOS → bootloader → kernel**, it goes **S3 wakeup → trampoline → protected mode → long mode → kernel resume**.

