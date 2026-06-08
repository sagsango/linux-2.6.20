```text
===============================================================================
FILE: arch/x86_64/kernel/traps.c
PURPOSE: x86-64 EXCEPTION, TRAP, NMI, DEBUG, OOPS, AND STACK TRACE HANDLING
KERNEL: Linux 2.6.x
===============================================================================

BACKGROUND
===============================================================================

This file is the main x86-64 CPU exception manager.

When CPU detects abnormal execution, it raises an exception/trap:

    divide by zero
    invalid opcode
    breakpoint
    debug exception
    general protection fault
    page fault
    NMI
    double fault
    machine check
    FPU/SIMD exception

The CPU enters the kernel through the IDT.

entry.S saves low-level state.

traps.c decides what to do next.

===============================================================================
HIGH LEVEL FLOW
===============================================================================

CPU exception
      |
      v
IDT vector
      |
      v
entry.S assembly stub
      |
      v
C handler in traps.c
      |
      +--> user fault?
      |       |
      |       v
      |   send signal
      |
      +--> kernel fault?
              |
              +--> exception table fixup?
              |       |
              |       v
              |   recover
              |
              +--> no fixup
                      |
                      v
                    oops / die

===============================================================================
WHY THIS FILE IS IMPORTANT
===============================================================================

This file connects many major kernel topics:

    IDT setup
    exception handling
    INT3
    kprobes
    ptrace
    hardware breakpoints
    NMI watchdog
    double fault
    machine check
    BUG()
    oops
    stack traces
    crash_kexec()
    signal delivery

If you understand this file, you understand how Linux reacts when the CPU says:

    "Something went wrong."

===============================================================================
IDT BASICS
===============================================================================

IDT = Interrupt Descriptor Table.

The CPU uses it to find exception handlers.

Example:

    vector 0  -> divide error
    vector 1  -> debug exception
    vector 2  -> NMI
    vector 3  -> INT3 breakpoint
    vector 8  -> double fault
    vector 13 -> general protection fault
    vector 14 -> page fault
    vector 18 -> machine check
    vector 19 -> SIMD exception

===============================================================================
trap_init()
===============================================================================

This function installs exception handlers into the IDT.

Example:

    set_intr_gate(0, &divide_error);

        vector 0 = divide error

    set_intr_gate_ist(2, &nmi, NMI_STACK);

        vector 2 = NMI using NMI IST stack

    set_system_gate_ist(3, &int3, DEBUG_STACK);

        vector 3 = INT3 callable from userspace

    set_intr_gate(14, &page_fault);

        vector 14 = page fault

===============================================================================
WHY SOME USE IST
===============================================================================

IST = Interrupt Stack Table.

x86-64 can switch to a special stack automatically for dangerous exceptions.

Used for:

    debug exception
    NMI
    double fault
    stack fault
    machine check

Reason:

    current kernel stack may be corrupted

So CPU switches to a known-good emergency stack.

===============================================================================
EXCEPTION STACKS
===============================================================================

Normal kernel stack:

    used for syscall/normal kernel execution

IRQ stack:

    used for interrupts

IST exception stacks:

    used for severe exceptions

Examples:

    DEBUG_STACK
    NMI_STACK
    DOUBLEFAULT_STACK
    STACKFAULT_STACK
    MCE_STACK

===============================================================================
USER FAULT VS KERNEL FAULT
===============================================================================

The most important decision:

    Did exception happen in user mode or kernel mode?

User mode:

    send signal

Kernel mode:

    try exception-table fixup

    otherwise oops

===============================================================================
USER MODE EXAMPLE
===============================================================================

User program:

    int x = 1 / 0;

CPU raises:

    #DE divide error

Kernel:

    do_divide_error()
        |
        v
    do_trap()
        |
        v
    force_sig(SIGFPE)

User process receives:

    SIGFPE

===============================================================================
KERNEL MODE EXAMPLE
===============================================================================

Kernel:

    *(int *)0 = 1;

CPU raises page fault.

If no fixup exists:

    die()
        |
        v
    show_registers()
        |
        v
    show_trace()
        |
        v
    oops

===============================================================================
do_trap()
===============================================================================

Generic helper for many exceptions.

Responsibilities:

    save trap number

    save error code

    if user mode:
        send signal

    if kernel mode:
        search exception table

    if no fixup:
        die()

===============================================================================
EXCEPTION TABLE FIXUP
===============================================================================

Very important kernel recovery mechanism.

Some kernel code intentionally touches userspace memory:

    copy_from_user()
    copy_to_user()
    get_user()
    put_user()

These can fault.

But kernel should not crash.

So assembly creates exception table entries:

    faulting instruction -> recovery instruction

When fault happens:

    search_exception_tables(regs->rip)

If found:

    regs->rip = fixup address

Kernel continues safely.

===============================================================================
EXCEPTION TABLE FLOW
===============================================================================

kernel executes copy_from_user()
      |
      v
bad user pointer faults
      |
      v
trap handler
      |
      v
search_exception_tables()
      |
      +--> fixup found
              |
              v
          regs->rip = fixup
              |
              v
          return from exception

===============================================================================
die()
===============================================================================

Called when kernel fault cannot be recovered.

Flow:

    oops_begin()
        |
        v
    report_bug()
        |
        v
    __die()
        |
        v
    show_registers()
        |
        v
    oops_end()
        |
        v
    do_exit(SIGSEGV)

===============================================================================
oops_begin()
===============================================================================

Prepares for printing crash information.

Does:

    oops_enter()

    disables interrupts

    tries to acquire die_lock

    makes console verbose

    bust_spinlocks(1)

Why?

During crash, printk must work as much as possible.

===============================================================================
__die()
===============================================================================

Prints the real oops information.

Includes:

    exception name

    error code

    CPU state

    RIP

    RSP

    process name

    register dump

    call trace

Also notifies die chain:

    notify_die()

And may trigger:

    crash_kexec()

===============================================================================
show_registers()
===============================================================================

Prints CPU state at fault time.

Includes:

    RIP
    RSP
    RAX-R15
    EFLAGS
    process name
    task pointer
    thread_info pointer

If fault happened in kernel mode:

    prints stack

    prints bytes at RIP

===============================================================================
STACK TRACE ENGINE
===============================================================================

This file includes:

    dump_trace()
    show_trace()
    show_stack()
    dump_stack()

These walk kernel stack memory and print likely return addresses.

===============================================================================
WHY STACK TRACE IS HARD ON x86-64
===============================================================================

There may be multiple stacks:

    process stack
    IRQ stack
    exception stack

A crash can happen while nested:

    process stack
        |
        v
    IRQ stack
        |
        v
    NMI stack

dump_trace() knows how to walk across them.

===============================================================================
dump_trace()
===============================================================================

Core stack walker.

It scans stack words.

If a word looks like a kernel text address:

    __kernel_text_address(addr)

then it reports it as a possible return address.

Callbacks decide what to do:

    print address

    save address

    mark stack context

===============================================================================
STACK CONTEXT MARKERS
===============================================================================

During traces, it can print markers like:

    <IRQ>
    <EOI>
    <NMI>
    <#DB>
    <#DF>

These show that the call trace crossed into another stack context.

===============================================================================
INT3 HANDLING
===============================================================================

INT3 is vector 3.

Instruction opcode:

    0xCC

Used by:

    GDB software breakpoints
    kprobes
    uprobes
    BUG/debugging paths

Handler:

    do_int3()

===============================================================================
INT3 FLOW
===============================================================================

CPU executes 0xCC
      |
      v
#BP breakpoint exception
      |
      v
IDT vector 3
      |
      v
int3 assembly stub
      |
      v
do_int3()
      |
      v
notify_die(DIE_INT3)
      |
      +--> kprobe/debugger may consume it
      |
      v
otherwise do_trap(SIGTRAP)

===============================================================================
WHY notify_die() MATTERS
===============================================================================

Before normal trap handling, this file calls:

    notify_die()

This lets special subsystems intercept exceptions.

Examples:

    kprobes
    kdb/kgdb
    crash tools
    debug hooks

If notifier returns NOTIFY_STOP:

    traps.c stops normal handling.

===============================================================================
DEBUG EXCEPTION #DB
===============================================================================

Vector 1.

Handler:

    do_debug()

Generated by:

    trap flag single-step

    hardware breakpoints

    watchpoints

    debug register events

===============================================================================
do_debug() FLOW
===============================================================================

read DR6
      |
      v
notify_die(DIE_DEBUG)
      |
      v
handle hardware breakpoint / single-step
      |
      v
send SIGTRAP if needed

===============================================================================
DR REGISTERS
===============================================================================

DR0-DR3:

    breakpoint/watchpoint addresses

DR6:

    debug status

DR7:

    debug control

If DR7 says watchpoint is active and address matches:

    CPU raises #DB

===============================================================================
SINGLE STEP
===============================================================================

Single step uses:

    RFLAGS.TF

TF = Trap Flag.

If TF is set:

    CPU raises #DB after one instruction.

Used by:

    ptrace
    GDB step/next

===============================================================================
NMI HANDLING
===============================================================================

NMI = Non-Maskable Interrupt.

Vector 2.

Handler:

    default_do_nmi()

NMI cannot be disabled by CLI.

Used for:

    NMI watchdog
    hardware errors
    memory parity
    I/O check
    perf/debug events

===============================================================================
NMI FLOW
===============================================================================

NMI arrives
      |
      v
CPU switches to NMI_STACK
      |
      v
nmi entry stub
      |
      v
default_do_nmi()
      |
      +--> check external NMI reason
      |
      +--> NMI watchdog?
      |
      +--> notifier callback?
      |
      +--> unknown NMI handling

===============================================================================
NMI WATCHDOG
===============================================================================

If CPU gets stuck with interrupts disabled, normal timer IRQs may not run.

But NMI can still arrive.

So NMI watchdog can detect:

    hard lockup

Flow:

    NMI
      |
      v
    nmi_watchdog_tick()
      |
      v
    if CPU not making progress:
        report lockup

===============================================================================
DOUBLE FAULT
===============================================================================

Vector 8.

Handler:

    do_double_fault()

Runs on:

    DOUBLEFAULT_STACK

Double fault means:

    fault occurred while trying to handle another fault

This is extremely serious.

Linux treats it as non-recoverable.

===============================================================================
DOUBLE FAULT FLOW
===============================================================================

first fault
      |
      v
fault during fault handling
      |
      v
#DF
      |
      v
double fault stack
      |
      v
do_double_fault()
      |
      v
die forever

===============================================================================
GENERAL PROTECTION FAULT
===============================================================================

Vector 13.

Handler:

    do_general_protection()

User mode:

    send SIGSEGV

Kernel mode:

    try exception table fixup

    otherwise die()

Possible causes:

    invalid segment selector
    privileged instruction
    bad descriptor
    canonical-address violation
    illegal CPU state transition

===============================================================================
MACHINE CHECK
===============================================================================

Vector 18.

Installed if CONFIG_X86_MCE.

Runs on:

    MCE_STACK

Machine check usually means hardware detected a severe problem:

    ECC memory error
    cache error
    bus error
    CPU internal error

Handler itself is elsewhere, but trap_init installs the IDT entry here.

===============================================================================
FPU / SIMD EXCEPTIONS
===============================================================================

x87 exception:

    vector 16
    do_coprocessor_error()

SSE/SIMD exception:

    vector 19
    do_simd_coprocessor_error()

User mode:

    send SIGFPE with detailed si_code

Kernel mode:

    try math exception fixup

    otherwise die

===============================================================================
math_state_restore()
===============================================================================

Handles lazy FPU restore.

If task uses FPU and state is not loaded:

    device-not-available exception occurs

Then:

    math_state_restore()
        |
        v
    clts()
        |
        v
    init or restore FPU state
        |
        v
    mark task used FPU

===============================================================================
BUG HANDLING
===============================================================================

BUG() often emits invalid opcode:

    ud2

Function:

    is_valid_bugaddr()

checks whether RIP points to UD2 instruction:

    0x0f 0x0b

If yes, kernel recognizes it as intentional BUG.

===============================================================================
KPROBES RELATION
===============================================================================

Kprobes insert INT3 into kernel text.

Flow:

    patched instruction -> INT3
        |
        v
    do_int3()
        |
        v
    notify_die(DIE_INT3)
        |
        v
    kprobe handler consumes trap

So INT3 is not only for GDB.

It is also used for kernel instrumentation.

===============================================================================
PTRACE RELATION
===============================================================================

GDB using ptrace can insert INT3 into userspace.

Flow:

    GDB writes 0xCC
      |
      v
    user process executes INT3
      |
      v
    do_int3()
      |
      v
    SIGTRAP
      |
      v
    debugger observes stop

Single-step uses #DB instead:

    TF -> #DB -> do_debug() -> SIGTRAP

===============================================================================
CRASH KEXEC RELATION
===============================================================================

If kernel crashes and kdump is configured:

    kexec_should_crash(current)

then:

    crash_kexec(regs)

This starts crash kernel capture path.

So traps.c is also part of kdump crash handling.

===============================================================================
COMPLETE EXAMPLE: USER DIVIDE BY ZERO
===============================================================================

user code:
    1 / 0
      |
      v
CPU raises #DE
      |
      v
divide_error assembly stub
      |
      v
do_divide_error()
      |
      v
do_trap()
      |
      v
force_sig_info(SIGFPE)
      |
      v
user receives SIGFPE

===============================================================================
COMPLETE EXAMPLE: GDB BREAKPOINT
===============================================================================

GDB patches user code with 0xCC
      |
      v
program executes INT3
      |
      v
CPU raises #BP
      |
      v
do_int3()
      |
      v
SIGTRAP
      |
      v
GDB wakes up

===============================================================================
COMPLETE EXAMPLE: KERNEL BAD POINTER
===============================================================================

kernel dereferences bad pointer
      |
      v
CPU raises fault
      |
      v
handler checks exception table
      |
      +--> fixup found:
      |       recover
      |
      +--> no fixup:
              die()
                |
                v
              oops

===============================================================================
COMPLETE EXAMPLE: CPU STUCK
===============================================================================

CPU stuck with IRQs disabled
      |
      v
timer interrupts cannot run
      |
      v
NMI watchdog still fires
      |
      v
default_do_nmi()
      |
      v
nmi_watchdog_tick()
      |
      v
hard lockup detected

===============================================================================
MENTAL MODEL
===============================================================================

entry.S:

    low-level CPU entry/exit

traps.c:

    exception policy and diagnostics

signal.c:

    sends/restores user signals

ptrace.c:

    debugger control

stacktrace.c:

    reusable stack trace saving

process.c:

    task CPU-state switching

traps.c ties them together when something abnormal happens.

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/traps.c is the central x86-64 exception manager:
it installs IDT entries, handles CPU traps such as INT3, #DB, #GP,
NMI, double fault, machine check, and FPU/SIMD faults, routes user
exceptions into signals, recovers kernel faults through exception-table
fixups when possible, and produces oops/register/stack diagnostics when
the kernel cannot safely recover. :contentReference[oaicite:0]{index=0}
===============================================================================
```

