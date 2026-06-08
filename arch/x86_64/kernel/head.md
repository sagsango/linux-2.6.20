```text
===============================================================================
x86_64 EARLY BOOT: 32-BIT TO 64-BIT TRANSITION
File: arch/x86_64/kernel/head.S
===============================================================================

PURPOSE
=======

This file is one of the first x86_64 kernel files executed.

Its main job:

    Start in 32-bit protected mode
    Build minimal CPU state
    Enable PAE
    Load early page tables
    Enable long mode
    Jump into 64-bit kernel C code


In simple words:

    head.S brings the CPU from early boot mode into real x86_64 Linux.


===============================================================================
BIG PICTURE
===============================================================================

Bootloader
     |
     v
startup_32
     |
     +--> check CPU supports long mode
     |
     +--> enable PAE
     |
     +--> load CR3 with early PML4
     |
     +--> enable EFER.LME
     |
     +--> enable paging
     |
     v
32-bit compatibility mode inside long mode
     |
     v
far jump
     |
     v
startup_64
     |
     +--> reload CR3
     |
     +--> setup CR0/CR4/EFER
     |
     +--> setup stack
     |
     +--> load final GDT
     |
     +--> setup dummy GS/PDA
     |
     v
x86_64_start_kernel()


===============================================================================
INITIAL CPU STATE AT startup_32
===============================================================================

At entry:

    CPU is in 32-bit protected mode
    paging is OFF
    long mode is OFF
    no normal kernel stack yet


Important comment:

    The kernel cannot immediately jump to final virtual address space.

Why?

Because early setup still needs identity mappings.

So early page tables map both:

    1. low physical identity area
    2. high kernel virtual address area


===============================================================================
WHY IDENTITY MAPPING IS NEEDED
===============================================================================

Early code is executing at a low physical address.

Example:

    physical 0x00100000

Before final kernel virtual mapping works, CPU needs:

    virtual 0x00100000 -> physical 0x00100000

This is identity mapping.


Later kernel wants to run at:

    0xffffffff80000000

So early page tables provide both:

    low identity map
    high kernel map


Diagram:

Early Page Tables:

    VA 0x0000000000100000
        |
        v
    PA 0x0000000000100000


    VA 0xffffffff80000000
        |
        v
    PA 0x0000000000000000


===============================================================================
startup_32 FLOW
===============================================================================

startup_32
    |
    +--> load data segment
    |
    +--> load temporary GDT
    |
    +--> check CPUID extended functions
    |
    +--> check long mode support
    |
    +--> enable CR4.PAE
    |
    +--> load CR3 with boot_level4_pgt
    |
    +--> set EFER.LME
    |
    +--> set CR0.PG and CR0.PE
    |
    +--> far jump to startup_64


===============================================================================
STEP 1 : LOAD DATA SEGMENT
===============================================================================

Code:

    movl $__KERNEL_DS, %eax
    movl %eax, %ds


Meaning:

    Setup data segment selector.

Even in x86_64, early transition still needs valid segment registers.


===============================================================================
STEP 2 : LOAD TEMPORARY GDT
===============================================================================

Code:

    lgdt pGDT32 - __START_KERNEL_map


GDT contains descriptors for:

    32-bit code/data
    64-bit kernel code
    kernel data


Why needed?

To jump from 32-bit protected mode into 64-bit code segment.

Long mode requires a code segment with:

    L bit = 1


===============================================================================
STEP 3 : CHECK LONG MODE SUPPORT
===============================================================================

Code flow:

    cpuid 0x80000000
        check extended CPUID exists

    cpuid 0x80000001
        check EDX bit 29


EDX bit 29 means:

    Long Mode supported


If not:

    no_long_mode:
        loop forever


Flow:

CPUID
  |
  +--> Long mode supported?
          |
          +--> no  -> hang
          |
          +--> yes -> continue


===============================================================================
STEP 4 : ENABLE PAE
===============================================================================

Code:

    btsl $5, %eax
    movl %eax, %cr4


CR4.PAE = 1

Why?

x86_64 long mode requires PAE paging structures.

Long mode page tables use:

    PML4
    PDP
    PD
    PT


===============================================================================
STEP 5 : LOAD EARLY PML4
===============================================================================

Code:

    movl $(boot_level4_pgt - __START_KERNEL_map), %eax
    movl %eax, %cr3


CR3 points to:

    boot_level4_pgt


This is the early 4-level page table root.


===============================================================================
STEP 6 : ENABLE LONG MODE
===============================================================================

Code:

    movl $MSR_EFER, %ecx
    rdmsr
    btsl $_EFER_LME, %eax
    wrmsr


EFER.LME = Long Mode Enable

Important:

    This does not fully enter long mode yet.

Long mode becomes active only after paging is enabled.


===============================================================================
STEP 7 : ENABLE PAGING
===============================================================================

Code:

    CR0.PG = 1
    CR0.PE = 1
    movl %eax, %cr0


Now conditions are:

    CR4.PAE = 1
    EFER.LME = 1
    CR0.PG = 1


CPU enters:

    Long mode active,
    but still executing 32-bit compatibility code

because current CS is still a 32-bit code segment.


===============================================================================
STEP 8 : FAR JUMP TO 64-BIT CODE
===============================================================================

Code:

    ljmp $__KERNEL_CS, startup_64


Why far jump?

It reloads CS with a 64-bit code segment.

After this:

    CS.L = 1

CPU is now executing true 64-bit code.


Transition:

    32-bit protected mode
          |
          v
    enable PAE
          |
          v
    enable EFER.LME
          |
          v
    enable paging
          |
          v
    compatibility mode
          |
          v
    far jump
          |
          v
    64-bit long mode


===============================================================================
startup_64 FLOW
===============================================================================

startup_64
    |
    +--> enable CR4.PAE and CR4.PGE
    |
    +--> reload CR3
    |
    +--> check NX support
    |
    +--> enable EFER.SCE
    |
    +--> enable EFER.NX if supported
    |
    +--> set final CR0 bits
    |
    +--> setup boot stack
    |
    +--> clear EFLAGS
    |
    +--> load final GDT
    |
    +--> setup dummy GS/PDA
    |
    +--> setup data segments
    |
    +--> pass boot params in RDI
    |
    +--> far return to x86_64_start_kernel


===============================================================================
WHY startup_64 RELOADS PAGE TABLES
===============================================================================

startup_64 can be reached in two ways:

    1. from startup_32
    2. directly from a 64-bit bootloader


So it reloads page tables to guarantee known state.

Code:

    movq $(boot_level4_pgt - __START_KERNEL_map), %rax
    movq %rax, %cr3


===============================================================================
ENABLE PGE
===============================================================================

Code:

    CR4.PGE = 1


PGE = Page Global Enable

Allows global TLB entries.

Useful for kernel mappings that should not be flushed on every address
space switch.


===============================================================================
ENABLE SYSCALL
===============================================================================

Code:

    EFER.SCE = 1


SCE = System Call Extensions

Enables:

    SYSCALL
    SYSRET


Later used by entry.S system_call path.


===============================================================================
ENABLE NX IF SUPPORTED
===============================================================================

CPUID:

    0x80000001

EDX bit 20:

    NX support


If supported:

    EFER.NX = 1


NX = No Execute

Allows page tables to mark pages non-executable.


===============================================================================
SET FINAL CR0
===============================================================================

CR0 bits enabled:

    PE  = protected mode
    MP  = monitor coprocessor
    ET  = extension type
    NE  = numeric error
    WP  = write protect
    AM  = alignment mask
    PG  = paging


Important one:

    CR0.WP

Makes kernel respect read-only page protections.


===============================================================================
SETUP BOOT STACK
===============================================================================

Code:

    movq init_rsp(%rip), %rsp


init_rsp points to:

    init_thread_union + THREAD_SIZE - 8


Diagram:

init_thread_union
    |
    +----------------------+
    | initial task stack   |
    |                      |
    |                  RSP |
    +----------------------+


===============================================================================
CLEAR EFLAGS
===============================================================================

Code:

    pushq $0
    popfq


Purpose:

    Start C kernel with clean flags.


===============================================================================
LOAD FINAL GDT
===============================================================================

Code:

    lgdt cpu_gdt_descr


Why reload GDT?

The final GDT lives at kernel virtual address.

Early 32-bit GDT used physical/identity style addressing.

Now kernel is ready to use real 64-bit kernel descriptors.


===============================================================================
SETUP DUMMY PDA / GS
===============================================================================

Code:

    MSR_GS_BASE = empty_zero_page


Old x86_64 kernels used PDA:

    Per-CPU Data Area


Some early code may call:

    in_interrupt()


That code expects %gs based PDA to exist.

So head.S installs a dummy PDA pointing to empty_zero_page.


Later real per-CPU GS/PDA is installed.


===============================================================================
SETUP DATA SEGMENTS
===============================================================================

Code:

    movl $__KERNEL_DS, %eax
    movl %eax, %ds
    movl %eax, %ss
    movl %eax, %es


Even though segmentation is mostly disabled in long mode,
valid selectors are still required for some checks and returns.


===============================================================================
PASS BOOT PARAMETER POINTER TO C
===============================================================================

Code:

    movl %esi, %edi


x86_64 C calling convention:

    first argument = RDI


So this passes the real-mode boot parameter structure pointer to:

    x86_64_start_kernel()


===============================================================================
JUMP TO C KERNEL
===============================================================================

Code:

    movq initial_code(%rip), %rax

    pushq $0
    pushq $__KERNEL_CS
    pushq %rax
    lretq


initial_code:

    x86_64_start_kernel


Why lretq?

It loads CS and RIP together.

This ensures:

    correct 64-bit kernel code segment
    jump to high virtual kernel address


Flow:

startup_64
    |
    v
lretq
    |
    v
x86_64_start_kernel()


===============================================================================
EARLY IDT HANDLER
===============================================================================

Function:

    early_idt_handler


Purpose:

Handle unexpected exceptions before real IDT is ready.


If exception happens very early:

    print:

    PANIC: early exception rip ... error ... cr2 ...


Then:

    hlt forever


This is useful for early boot debugging.


===============================================================================
NO LONG MODE PATH
===============================================================================

Label:

    no_long_mode


If CPU does not support x86_64 long mode:

    loop forever


Because x86_64 kernel cannot run on non-long-mode CPU.


===============================================================================
EARLY PAGE TABLES
===============================================================================

This file defines early boot page tables.

Main tables:

    boot_level4_pgt
    level3_ident_pgt
    level2_ident_pgt
    level3_kernel_pgt
    level2_kernel_pgt
    level3_physmem_pgt


===============================================================================
boot_level4_pgt
===============================================================================

This is the early PML4.

It maps:

    1. identity mapping
    2. physical memory mapping
    3. high kernel mapping


Layout idea:

PML4
 |
 +--> level3_ident_pgt
 |
 +--> level3_physmem_pgt
 |
 +--> level3_kernel_pgt


===============================================================================
IDENTITY MAPPING
===============================================================================

level3_ident_pgt
    |
    v
level2_ident_pgt


level2_ident_pgt maps first 40MB using 2MB pages.

Code:

    .rept 20
        .quad i << 21 | 0x083


20 entries * 2MB = 40MB


Purpose:

    early code can continue executing at low physical addresses.


===============================================================================
KERNEL HIGH MAPPING
===============================================================================

level3_kernel_pgt
    |
    v
level2_kernel_pgt


Maps kernel virtual address:

    0xffffffff80000000

to physical memory.

Also maps first 40MB using 2MB pages.


Purpose:

    allow jump into high kernel virtual address space.


===============================================================================
PHYSMEM MAPPING
===============================================================================

level3_physmem_pgt
    |
    v
level2_kernel_pgt


Purpose:

    make __va() work early before full page tables exist.


===============================================================================
2MB LARGE PAGES
===============================================================================

Entries use large page bit.

Instead of:

    PML4 -> PDP -> PD -> PT -> 4KB page

Early boot uses:

    PML4 -> PDP -> PD -> 2MB page


Why?

    simpler
    fewer tables
    enough for early boot


===============================================================================
GDT TABLE
===============================================================================

cpu_gdt_table contains descriptors:

    NULL
    __KERNEL_CS
    __KERNEL_DS
    __USER32_CS
    __USER_DS
    __USER_CS
    __KERNEL32_CS
    TSS
    LDT
    TLS descriptors


Important descriptors:

__KERNEL_CS
------------

64-bit kernel code segment.


__KERNEL_DS
------------

Kernel data segment.


__USER_CS
----------

64-bit user code segment.


__USER_DS
----------

User data segment.


===============================================================================
IDT TABLE
===============================================================================

idt_table:

    256 entries * 16 bytes


This is the interrupt descriptor table storage.

Later initialized by C code.


===============================================================================
EMPTY ZERO PAGE
===============================================================================

empty_zero_page:

    one page of zeros


Used early for:

    dummy GS/PDA
    zero page purposes


===============================================================================
WAKEUP PAGE TABLES
===============================================================================

Under CONFIG_ACPI_SLEEP:

    wakeup_level4_pgt


Used by ACPI resume trampoline.

Connection to earlier file:

    wakeup.S uses wakeup_level4_pgt to re-enter long mode after S3 resume.


This mirrors boot-time long mode setup.


===============================================================================
COMPLETE CPU MODE TRANSITION
===============================================================================

Bootloader gives control
      |
      v
32-bit protected mode
      |
      v
startup_32
      |
      +--> verify long mode
      |
      +--> CR4.PAE = 1
      |
      +--> CR3 = boot_level4_pgt
      |
      +--> EFER.LME = 1
      |
      +--> CR0.PG = 1
      |
      v
Long mode active but 32-bit compatibility
      |
      v
far jump with 64-bit CS
      |
      v
64-bit long mode
      |
      v
startup_64
      |
      +--> setup CR0/CR4/EFER
      |
      +--> setup stack
      |
      +--> load GDT
      |
      +--> setup dummy GS
      |
      +--> pass boot params
      |
      v
x86_64_start_kernel()


===============================================================================
RELATION TO OTHER FILES
===============================================================================

head.S
------

Initial 32-bit to 64-bit transition.

entry.S
-------

Runtime syscall/interrupt/exception entry after kernel is alive.

e820.c
------

Parses firmware memory map after C code begins.

early_printk.c
--------------

Used by early_idt_handler to print early crash messages.

acpi.c / wakeup.S
-----------------

Use similar long mode transition logic during S3 resume.

apic.c
------

Runs later to setup APIC interrupt hardware.


===============================================================================
KEY IDEA
===============================================================================

head.S is the first major architecture bootstrap file.

Its job is to create the minimum CPU environment needed for C code:

    64-bit long mode
    paging enabled
    early page tables
    kernel stack
    valid GDT
    dummy GS/PDA
    boot parameters in RDI


After this file jumps to:

    x86_64_start_kernel()

the kernel is finally running as a real 64-bit C program.
===============================================================================
```

