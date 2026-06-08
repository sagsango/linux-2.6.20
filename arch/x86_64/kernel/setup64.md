```text
============================================================
x86-64 CPU INITIALIZATION
File: arch/x86_64/kernel/setup64.c
Linux 2.6.x Era
============================================================

PURPOSE
------------------------------------------------------------

This file initializes per-CPU x86-64 state.

If setup.c answers:

    "What machine am I running on?"

then setup64.c answers:

    "How do I prepare each CPU to run Linux?"

It sets up:

    per-CPU areas
    PDA
    GS base
    syscall MSRs
    GDT
    IDT
    TSS
    IST exception stacks
    I/O bitmap
    debug registers
    FPU state
    NX support

============================================================
BIG PICTURE
============================================================

Boot CPU
    |
    v
head64.c does earliest setup
    |
    v
setup_arch()
    |
    v
setup_per_cpu_areas()
    |
    v
cpu_init()
    |
    v
CPU ready for scheduler / interrupts / syscalls

Application Processor
    |
    v
SMP trampoline
    |
    v
pda_init(cpu)
    |
    v
cpu_init()
    |
    v
CPU ready

============================================================
WHY THIS FILE EXISTS
------------------------------------------------------------

x86-64 CPUs have a lot of hidden state:

    GDT
    IDT
    TSS
    TR register
    FS base
    GS base
    syscall MSRs
    CR4 bits
    debug registers
    exception stacks

Generic kernel code cannot initialize this.

So every CPU must pass through this architecture-specific setup.

============================================================
KEY GLOBALS
============================================================

x86_boot_params

    Copy of boot parameters.

------------------------------------------------------------

cpu_initialized

    CPU mask tracking which CPUs already ran cpu_init().

------------------------------------------------------------

_cpu_pda[]

    Array of PDA pointers, one per CPU.

------------------------------------------------------------

boot_cpu_pda[]

    Static PDA storage for early boot CPUs.

------------------------------------------------------------

idt_descr

    IDT descriptor used by lidt.

------------------------------------------------------------

boot_cpu_stack

    Boot CPU interrupt stack.

------------------------------------------------------------

__supported_pte_mask

    Mask of allowed PTE bits.

    NX bit may be removed from this mask.

============================================================
WHAT IS PDA?
============================================================

PDA = Processor Data Area.

Old x86-64 Linux used PDA before modern per-cpu handling matured.

It is per-CPU data accessed through GS.

Each CPU has its own PDA:

CPU0 GS -> PDA0
CPU1 GS -> PDA1
CPU2 GS -> PDA2

PDA stores:

    current task

    CPU number

    kernel stack

    IRQ stack pointer

    active_mm

    idle flag

============================================================
WHY GS?
============================================================

In x86-64, FS/GS can have a 64-bit base address.

Linux uses GS base for fast per-CPU access.

Example conceptually:

    %gs:pcurrent -> current task

So reading current can be very fast.

============================================================
PDA FLOW
============================================================

CPU starts
    |
    v
pda_init(cpu)
    |
    v
wrmsr(MSR_GS_BASE, pda)
    |
    v
%gs now points to this CPU's PDA

============================================================
NX SUPPORT
============================================================

NX = No Execute.

Page table bit that marks memory non-executable.

Used for:

    non-executable stack
    non-executable heap
    W^X style protection

Boot option:

    noexec=on
    noexec=off

============================================================
nonx_setup()
============================================================

If:

    noexec=on

then:

    allow _PAGE_NX

If:

    noexec=off

then:

    remove _PAGE_NX from supported PTE mask

============================================================
check_efer()
============================================================

Reads:

    MSR_EFER

Checks:

    EFER_NX

If CPU does not support NX:

    remove _PAGE_NX

So page table code will not use NX on unsupported CPUs.

============================================================
noexec32=
============================================================

Controls 32-bit process execute behavior.

noexec32=on

    PROT_READ does not imply PROT_EXEC

noexec32=off

    old compatibility behavior:
    readable mappings are executable

============================================================
setup_per_cpu_areas()
============================================================

Purpose:

    allocate per-CPU memory for each possible CPU.

Flow:

    for each possible CPU:
        allocate PERCPU_ENOUGH_ROOM
        copy __per_cpu_start -> new area
        set cpu_pda(cpu)->data_offset

============================================================
WHY COPY PER-CPU SECTION?
============================================================

Kernel has one template:

    __per_cpu_start ... __per_cpu_end

But each CPU needs its own copy.

CPU0 variable:

    per_cpu(x,0)

CPU1 variable:

    per_cpu(x,1)

are different memory locations.

============================================================
PER-CPU DIAGRAM
============================================================

Template:

__per_cpu_start
    varA
    varB
__per_cpu_end

At boot:

CPU0 copy:
    varA0
    varB0

CPU1 copy:
    varA1
    varB1

CPU2 copy:
    varA2
    varB2

============================================================
pda_init(cpu)
============================================================

Purpose:

    initialize PDA for one CPU.

Steps:

    clear FS/GS selector

    write MSR_GS_BASE = pda

    set CPU number

    set irq count

    set kernel stack

    set active_mm

    set current task

    allocate IRQ stack

============================================================
WHY CLEAR FS/GS SELECTORS FIRST?
============================================================

Code:

    movl 0, %fs
    movl 0, %gs

Then:

    wrmsr(MSR_GS_BASE, pda)

Reason:

    avoid stale segment selector/base interactions.

The kernel wants GS base controlled by MSR.

============================================================
IRQ STACK
============================================================

Each CPU has a separate IRQ stack.

Why?

Interrupts can arrive while current task is deep in kernel stack.

Using a separate IRQ stack avoids overflowing task kernel stack.

CPU0:

    boot_cpu_stack

Other CPUs:

    allocated with __get_free_pages()

============================================================
EXCEPTION STACKS / IST
============================================================

File defines:

    boot_exception_stacks

Used for dangerous exceptions.

x86-64 has IST:

    Interrupt Stack Table

Stored inside TSS.

Exceptions can automatically switch to special stacks.

Examples:

    double fault

    NMI

    debug

    machine check

============================================================
WHY IST EXISTS?
============================================================

Suppose normal kernel stack is corrupted.

Then exception handling on same stack may crash.

IST lets CPU switch to known-good stack for critical exceptions.

============================================================
syscall_init()
============================================================

Initializes fast syscall entry.

Registers:

    MSR_STAR
    MSR_LSTAR
    MSR_SYSCALL_MASK

============================================================
MSR_LSTAR
============================================================

Contains 64-bit syscall entry address:

    system_call

When user executes:

    syscall

CPU jumps to:

    MSR_LSTAR

============================================================
MSR_STAR
============================================================

Contains kernel/user code segment selectors.

Used by syscall/sysret machinery.

============================================================
MSR_SYSCALL_MASK
============================================================

Flags to clear on syscall entry.

Clears:

    TF  trap flag
    DF  direction flag
    IF  interrupt flag
    IOPL bits

Why?

User should not enter kernel with dangerous flags active.

============================================================
SYSCALL FLOW
============================================================

User process
    |
    v
syscall instruction
    |
    v
CPU reads MSR_LSTAR
    |
    v
jumps to system_call
    |
    v
kernel syscall handler

============================================================
cpu_init()
============================================================

Most important function.

This is the per-CPU state barrier.

After cpu_init():

    CPU has valid GDT
    CPU has valid IDT
    CPU has valid TSS
    CPU has valid syscall MSRs
    CPU has valid exception stacks
    CPU has clean debug regs
    CPU has initialized FPU

============================================================
cpu_init() FLOW
============================================================

cpu_init()
    |
    +--> pda_init(cpu)
    |
    +--> zap low mappings
    |
    +--> mark CPU initialized
    |
    +--> clear CR4 features
    |
    +--> load GDT
    |
    +--> load IDT
    |
    +--> clear TLS
    |
    +--> setup syscall MSRs
    |
    +--> clear FS/GS bases
    |
    +--> check NX support
    |
    +--> allocate exception stacks
    |
    +--> setup TSS
    |
    +--> setup I/O bitmap
    |
    +--> setup active_mm
    |
    +--> load TR
    |
    +--> load LDT
    |
    +--> clear debug registers
    |
    +--> init FPU
    |
    +--> save kernel eflags

============================================================
GDT SETUP
============================================================

GDT = Global Descriptor Table.

Still needed in x86-64 for:

    code segments
    data segments
    TSS descriptor
    TLS descriptors
    compatibility mode

Flow:

    copy boot GDT to per-CPU GDT

    lgdt cpu_gdt_descr[cpu]

============================================================
IDT SETUP
============================================================

IDT = Interrupt Descriptor Table.

Contains handlers for:

    exceptions
    interrupts
    system traps

Flow:

    lidt idt_descr

============================================================
TSS SETUP
============================================================

TSS = Task State Segment.

In x86-64 Linux, not used for hardware task switching.

Used for:

    rsp0

    IST stacks

    I/O bitmap

============================================================
TSS CONTENTS
============================================================

tss->rsp0

    kernel stack when entering from user mode

------------------------------------------------------------

tss->ist[]

    special exception stacks

------------------------------------------------------------

tss->io_bitmap

    per-task I/O port permission map

============================================================
I/O BITMAP
============================================================

Set to all 1s:

    no I/O ports allowed

Code:

    t->io_bitmap[i] = ~0UL

Meaning:

    userspace cannot execute in/out unless explicitly allowed.

============================================================
TR REGISTER
============================================================

Task Register points to current CPU's TSS descriptor.

Flow:

    set_tss_desc(cpu, t)

    load_TR_desc()

Now CPU knows where TSS is.

============================================================
LDT LOAD
============================================================

Function:

    load_LDT(&init_mm.context)

Loads Local Descriptor Table for init_mm.

Usually empty on 64-bit Linux.

Needed for compatibility / modify_ldt.

============================================================
DEBUG REGISTER CLEAR
============================================================

Clears:

    DR0
    DR1
    DR2
    DR3
    DR6
    DR7

Why?

Avoid stale hardware breakpoints from firmware/previous state.

============================================================
FPU INIT
============================================================

Function:

    fpu_init()

Initializes x87/SSE state handling.

Important before user tasks use floating point.

============================================================
CR4 CLEANUP
============================================================

Clears:

    VME
    PVI
    TSD
    DE

This puts CPU in known kernel state.

============================================================
CPU0 VS AP CPUs
============================================================

CPU0:

    partially initialized in head64.c

    uses boot_exception_stacks

    uses boot_cpu_stack

------------------------------------------------------------

AP CPUs:

    initialized later during SMP boot

    pda_init(cpu)

    allocate IRQ stack

    allocate exception stacks

============================================================
COMPLETE BOOT CPU FLOW
============================================================

head64.c
    |
    v
early CPU0 setup
    |
    v
setup_arch()
    |
    v
setup_per_cpu_areas()
    |
    v
cpu_init()
    |
    v
CPU0 ready

============================================================
COMPLETE AP CPU FLOW
============================================================

BSP sends INIT/SIPI
    |
    v
AP trampoline
    |
    v
AP enters 64-bit kernel
    |
    v
pda_init(cpu)
    |
    v
cpu_init()
    |
    v
AP ready for scheduler

============================================================
RELATION TO process.c
============================================================

setup64.c:

    initializes per-CPU CPU state

process.c:

    switches per-task CPU state

Example:

setup64.c initializes:

    TSS
    GDT
    IDT
    PDA

process.c switches:

    rsp0
    FS/GS
    TLS
    debug registers
    I/O bitmap

============================================================
RELATION TO ptrace.c
============================================================

setup64.c clears debug registers initially.

ptrace.c later allows debugger to set:

    DR0-DR7

for hardware breakpoints.

============================================================
RELATION TO syscall path
============================================================

setup64.c programs:

    MSR_LSTAR = system_call

After that, user-mode syscall instruction can enter kernel.

Without syscall_init():

    syscall would not know where to jump.

============================================================
MENTAL MODEL
============================================================

setup.c:

    discover machine

setup64.c:

    initialize each CPU

process.c:

    switch between tasks

ptrace.c:

    debug tasks

mpparse.c:

    discover CPUs/APICs

smpboot.c:

    start other CPUs

============================================================
ONE-LINE SUMMARY
============================================================

setup64.c prepares each x86-64 CPU to run Linux by allocating
per-CPU areas, installing the PDA through GS base, initializing
syscall MSRs, loading GDT/IDT/TSS/LDT state, setting up IST
exception stacks and IRQ stacks, configuring NX support, clearing
debug registers, initializing FPU state, and making the CPU safe
for interrupts, syscalls, scheduling, and user/kernel transitions.
============================================================
```

