If you want a **single IDE-style study file** for this `arch/x86_64/kernel/kprobes.c`, I'd structure it like this:

```text
===========================================================
Linux KProbes (x86_64)
File: arch/x86_64/kernel/kprobes.c
===========================================================

GOAL
-----------------------------------------------------------
Dynamically insert breakpoints into running kernel code.

Used by:
    - KProbes
    - KRetProbes
    - JProbes (old kernels)
    - Tracing
    - Debugging
    - Performance analysis

===========================================================
1. HIGH LEVEL FLOW
===========================================================

register_kprobe()
        |
        v
arch_prepare_kprobe()
        |
        +--> allocate instruction slot
        +--> copy original instruction
        +--> fix RIP-relative addressing
        |
        v
arch_arm_kprobe()
        |
        +--> replace first byte with INT3
        |
        v
CPU executes code
        |
        v
INT3 exception
        |
        v
kprobe_handler()
        |
        +--> pre_handler()
        |
        +--> single-step original instruction
        |
        v
DEBUG exception (#DB)
        |
        v
post_kprobe_handler()
        |
        +--> post_handler()
        +--> restore RIP
        |
        v
continue execution


===========================================================
2. IMPORTANT DATA STRUCTURES
===========================================================

Per CPU:

DEFINE_PER_CPU(struct kprobe *, current_kprobe);

Current probe executing on this CPU.


DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

Stores:

    status
    saved flags
    nested probe state
    jprobe state


===========================================================
3. PROBE INSTALLATION
===========================================================

Function:

    arch_prepare_kprobe()

Flow:

    get_insn_slot()

            allocate executable memory

    arch_copy_kprobe()

            copy original instruction

            fix RIP-relative instructions

Example:

Original:

    mov foo(%rip), %rax

Copied:

    mov foo(%rip), %rax

Need new displacement because RIP changed.


===========================================================
4. RIP RELATIVE HANDLING
===========================================================

Function:

    is_riprel()

Purpose:

    Detect instructions like:

        mov foo(%rip), %rax

Returns:

    displacement location

Then:

    arch_copy_kprobe()

adjusts displacement.

Without this:

    copied instruction
    accesses wrong address.


===========================================================
5. ARMING A PROBE
===========================================================

Function:

    arch_arm_kprobe()

Original:

    55          push %rbp

Becomes:

    CC          int3

INT3 opcode:

    0xCC

Original opcode saved in:

    p->opcode


===========================================================
6. INT3 EXCEPTION PATH
===========================================================

CPU hits:

    int3

Exception:

    DIE_INT3

Notifier:

    kprobe_exceptions_notify()

            |
            v

    kprobe_handler()


===========================================================
7. kprobe_handler()
===========================================================

Main entry.

Flow:

    Find probe

    set_current_kprobe()

    call pre_handler()

    prepare_singlestep()

    return

State:

    KPROBE_HIT_ACTIVE
    KPROBE_HIT_SS
    KPROBE_REENTER
    KPROBE_HIT_SSDONE


===========================================================
8. SINGLE STEP EXECUTION
===========================================================

Function:

    prepare_singlestep()

Sets:

    TF = 1

Code:

    regs->eflags |= TF_MASK;

CPU:

    execute exactly one instruction

Then:

    #DB exception


===========================================================
9. WHY COPY THE INSTRUCTION?
===========================================================

Bad idea:

    restore original instruction
    single-step it

Problem:

    another CPU may execute it

Linux solution:

    keep INT3 in original code

    execute copied instruction instead

Original:

    int3

Copy:

    original instruction

Safe on SMP systems.


===========================================================
10. DEBUG EXCEPTION
===========================================================

CPU raises:

    #DB

Exception:

    DIE_DEBUG

Handler:

    post_kprobe_handler()

Flow:

    post_handler()

    resume_execution()

    restore RIP

    restore flags


===========================================================
11. RIP FIXUP AFTER STEP
===========================================================

Function:

    resume_execution()

Fixes:

    RIP

    CALL return addresses

    PUSHF flags

    JMP targets

Because CPU executed copied instruction,
not original instruction.


===========================================================
12. KRETPROBES
===========================================================

Purpose:

    probe function return

Function:

    arch_prepare_kretprobe()

Original stack:

    rsp
      |
      +--> caller_return_address

Replace:

    rsp
      |
      +--> kretprobe_trampoline


===========================================================
13. FUNCTION RETURN FLOW
===========================================================

Target Function
        |
        v
ret
        |
        v
kretprobe_trampoline
        |
        v
INT3
        |
        v
trampoline_probe_handler()
        |
        +--> call return handler
        +--> restore original return address
        |
        v
continue


===========================================================
14. JPROBES
===========================================================

(Old mechanism)

Purpose:

    access function arguments

Flow:

probe hit
    |
    v
setjmp_pre_handler()
    |
    +--> save registers
    +--> save stack
    +--> jump to user handler

Return:

    jprobe_return()

which triggers INT3 and restores state.


===========================================================
15. NESTED PROBES
===========================================================

Problem:

    Probe A hit

    While processing A

    Probe B hit

Solution:

    save_previous_kprobe()

            save state

    restore_previous_kprobe()

            restore state

Status:

    KPROBE_REENTER


===========================================================
16. FAULT HANDLING
===========================================================

Function:

    kprobe_fault_handler()

Handles:

    page fault

    general protection fault

Cases:

    single-step fault

    pre_handler fault

    post_handler fault

May:

    invoke user fault handler

or

    use exception table fixup


===========================================================
17. EXCEPTION DISPATCHER
===========================================================

Function:

    kprobe_exceptions_notify()

DIE_INT3
    |
    +--> kprobe_handler()

DIE_DEBUG
    |
    +--> post_kprobe_handler()

DIE_PAGE_FAULT
    |
    +--> kprobe_fault_handler()

DIE_GPF
    |
    +--> kprobe_fault_handler()


===========================================================
18. FILES TO READ NEXT
===========================================================

kernel/kprobes.c

include/linux/kprobes.h

arch/x86/kernel/kprobes/

kernel/trace/

kernel/trace/ftrace.c

kernel/events/


===========================================================
ONE-LINE SUMMARY
===========================================================

Replace instruction with INT3
        |
        v
Hit breakpoint
        |
        v
Run pre_handler
        |
        v
Single-step copied instruction
        |
        v
Run post_handler
        |
        v
Resume original execution
===========================================================
```

This is the kind of condensed "IDE notes file" I would keep open while reading the source. It maps almost every major function in `kprobes.c` to the execution flow. 
