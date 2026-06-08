```text
===============================================================================
FILE: arch/x86_64/kernel/trampoline.S
PURPOSE: REAL-MODE TRAMPOLINE USED TO BOOT SECONDARY CPUs
KERNEL: Linux 2.6.x x86-64
===============================================================================

BACKGROUND
===============================================================================

On an x86 SMP machine, only one CPU starts running after reset.

That CPU is:

    BSP = Bootstrap Processor

All other CPUs are:

    APs = Application Processors

At power-on:

    CPU0 / BSP  -> running Linux boot code
    CPU1 / AP   -> sleeping
    CPU2 / AP   -> sleeping
    CPU3 / AP   -> sleeping

Linux must wake the APs manually.

The APs do not start directly in 64-bit long mode.

They wake up in:

    real mode

with:

    16-bit code
    16-bit addressing
    no stack
    almost no environment

This file is the tiny bridge that gets an AP from real mode into
32-bit protected mode, then jumps to the normal x86-64 startup code.

===============================================================================
WHAT IS A TRAMPOLINE?
===============================================================================

A trampoline is small code placed at a special low physical address.

The BSP tells an AP:

    "Start executing at this low-memory address."

The AP begins there, runs this trampoline, and then jumps into the real
kernel startup path.

Think:

    sleeping AP
        |
        v
    real-mode trampoline
        |
        v
    protected-mode startup_32
        |
        v
    long-mode kernel
        |
        v
    start_secondary()

===============================================================================
WHY APs NEED LOW-MEMORY CODE
===============================================================================

Intel STARTUP IPI gives the AP a startup vector.

That vector points to a 4KB page below 1MB.

So the AP cannot be started at an arbitrary high kernel virtual address.

Linux copies this trampoline into low memory, usually around:

    SMP_TRAMPOLINE_BASE

Then sends SIPI with that address.

===============================================================================
RELATION TO smpboot.c
===============================================================================

smpboot.c does:

    setup_trampoline()
        |
        v
    copy trampoline_data to SMP_TRAMPOLINE_BASE

Then:

    wakeup_secondary_via_INIT()
        |
        +--> send INIT IPI
        +--> send STARTUP IPI with trampoline address

This file is what the AP executes after receiving SIPI.

===============================================================================
ENTRY CONDITIONS
===============================================================================

When AP starts executing trampoline_data:

    CPU is in real mode

    CS:IP points to trampoline start

    IP = 0

    16-bit addressing

    16-bit data mode

    no stack

    interrupts should be disabled

    data addresses must be absolute within the trampoline segment

This is why the comment warns:

    no relocation entries

===============================================================================
WHY NO RELOCATIONS?
===============================================================================

The trampoline is copied to low memory and executed there.

The linker may think symbols live at kernel virtual addresses.

But AP executes with real-mode segment addressing.

Therefore relocation entries would be dangerous.

The code must be position-safe relative to its copied location.

The comment says to check:

    objdump --full-contents --reloc

to ensure there are no relocations.

===============================================================================
TOP LEVEL FLOW
===============================================================================

trampoline_data:
    |
    +--> wbinvd
    |
    +--> set DS = CS
    |
    +--> cli
    |
    +--> write 0xA5A5A5A5 marker
    |
    +--> load empty IDT
    |
    +--> load temporary GDT
    |
    +--> enable protected mode
    |
    +--> far jump to startup_32

===============================================================================
STEP 1: wbinvd
===============================================================================

Instruction:

    wbinvd

Meaning:

    write back and invalidate CPU caches

Why?

The AP is just waking up.

Cache state may be stale or uncertain.

This makes memory state clean before transition.

===============================================================================
STEP 2: SET DS = CS
===============================================================================

Code:

    mov %cs, %ax
    mov %ax, %ds

Real mode uses segment registers.

Since code and data are in the same trampoline page, DS is made equal to CS.

Now data references like:

    idt_48 - r_base

work relative to the same segment.

===============================================================================
STEP 3: DISABLE INTERRUPTS
===============================================================================

Code:

    cli

Meaning:

    clear interrupt flag

No interrupts should occur during this fragile transition.

At this point:

    no real IDT
    no kernel stack
    no normal handlers

An interrupt here would be fatal.

===============================================================================
STEP 4: WRITE MARKER
===============================================================================

Code:

    movl $0xA5A5A5A5, trampoline_data - r_base

This writes a marker at the beginning of trampoline memory.

Purpose:

    BSP can detect that AP reached trampoline code.

In smpboot.c, if CPU fails to boot, it checks whether trampoline was touched.

If marker changed:

    AP started trampoline but got stuck later.

If not:

    AP never reached trampoline.

===============================================================================
STEP 5: LOAD EMPTY IDT
===============================================================================

Code:

    lidtl idt_48 - r_base

idt_48:

    limit = 0
    base  = 0

This installs an empty IDT.

Why acceptable?

Interrupts are disabled.

The AP is not expected to handle exceptions here.

This is minimal early setup.

===============================================================================
STEP 6: LOAD GDT
===============================================================================

Code:

    lgdtl gdt_48 - r_base

GDT descriptor points to:

    cpu_gdt_table - __START_KERNEL_map

This is the physical address of the kernel GDT table.

The GDT provides descriptors needed for protected mode.

===============================================================================
WHY lgdtl?
===============================================================================

In real mode, default operand size is 16-bit.

But the GDT address may require 32-bit operand size.

So code uses:

    lgdtl

to force 32-bit GDT descriptor load.

The comment explains:

    kernel can be beyond 16MB
    real-mode lgdt default operand size is too small

===============================================================================
STEP 7: ENABLE PROTECTED MODE
===============================================================================

Code:

    xor %ax, %ax
    inc %ax
    lmsw %ax

This sets:

    CR0.PE = 1

PE = Protected Enable

CPU is now in protected mode.

But after setting PE, the CPU needs a far jump to reload CS.

===============================================================================
STEP 8: FAR JUMP TO startup_32
===============================================================================

Code:

    ljmpl $__KERNEL32_CS, $(startup_32 - __START_KERNEL_map)

This does two things:

    loads CS with protected-mode code selector

    jumps to physical address of startup_32

Target:

    startup_32

in:

    arch/x86_64/kernel/head.S

===============================================================================
WHY STARTUP_32?
===============================================================================

AP cannot jump directly into 64-bit C code.

It must pass through normal low-level startup:

    startup_32
        |
        v
    enable paging / long mode
        |
        v
    64-bit entry
        |
        v
    start_secondary()

===============================================================================
WHY subtract __START_KERNEL_map?
===============================================================================

startup_32 is a kernel virtual address symbol.

But AP is running with physical addressing at this point.

So:

    startup_32 - __START_KERNEL_map

converts kernel virtual address to physical address.

===============================================================================
DATA STRUCTURES INSIDE TRAMPOLINE
===============================================================================

idt_48:

    descriptor for empty IDT

-------------------------------------------------------------------------------

gdt_48:

    descriptor for GDT

-------------------------------------------------------------------------------

trampoline_end:

    marks end of trampoline code/data

smpboot.c uses:

    trampoline_end - trampoline_data

to know how many bytes to copy.

===============================================================================
FULL AP BOOT FLOW
===============================================================================

BSP / CPU0
    |
    v
setup_trampoline()
    |
    v
copy trampoline to low memory
    |
    v
write warm reset vector
    |
    v
send INIT IPI
    |
    v
send STARTUP IPI
    |
    v

AP / CPU1 wakes in real mode
    |
    v
trampoline_data
    |
    +--> set DS
    +--> disable interrupts
    +--> load IDT/GDT
    +--> enable protected mode
    +--> far jump
    |
    v
startup_32
    |
    v
long mode setup
    |
    v
start_secondary()
    |
    v
cpu_init()
    |
    v
smp_callin()
    |
    v
CPU online

===============================================================================
WHY NO STACK?
===============================================================================

The comment says:

    "we don't actually need a stack so we don't set one up"

This code avoids:

    call
    push
    pop
    local variables

It uses only registers and direct memory operands.

That is deliberate.

A real-mode stack would require more setup and more assumptions.

===============================================================================
RELATION TO setup64.c
===============================================================================

trampoline.S:

    minimal AP transition from real mode to protected mode

setup64.c:

    full CPU setup after AP reaches kernel

Example:

    GDT
    IDT
    TSS
    PDA
    syscall MSRs
    exception stacks

are properly initialized later in:

    cpu_init()

===============================================================================
RELATION TO smpboot.c
===============================================================================

smpboot.c:

    orchestrates AP startup

trampoline.S:

    code AP actually executes first

smpboot.c sends:

    INIT/SIPI

trampoline.S responds by entering protected mode.

===============================================================================
RELATION TO head.S
===============================================================================

trampoline.S jumps to:

    startup_32

head.S then performs:

    protected-mode setup
    paging setup
    long-mode transition
    jump into 64-bit AP path

===============================================================================
MENTAL MODEL
===============================================================================

This file is not "normal kernel code."

It is closer to bootloader code.

It runs:

    before stack
    before C
    before long mode
    before normal kernel mappings
    before AP is online

It is tiny because the AP is still almost naked hardware.

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

trampoline.S is the tiny real-mode boot bridge for secondary CPUs: after
the BSP wakes an AP with INIT/SIPI, this code runs from low memory, sets up
minimal segment state, loads an empty IDT and kernel GDT, enables protected
mode, and far-jumps to startup_32 so the AP can continue through the normal
x86-64 long-mode kernel startup path.
===============================================================================
```

