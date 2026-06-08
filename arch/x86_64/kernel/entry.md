```text
===============================================================================
LINUX x86_64 ENTRY.S
File: arch/x86_64/kernel/entry.S
MASTER FLOW / ARCHITECTURE GUIDE
===============================================================================

PURPOSE
=======

This file is the entry gateway into the Linux kernel.

Every transition between:

    User Space
        <-->
    Kernel Space

passes through this file.

It handles:

    • System Calls
    • Interrupts
    • Exceptions
    • Signals
    • Scheduling checks
    • Fork return
    • Kernel thread startup
    • Return to userspace

Think of it as:

    "The front door of the kernel"

===============================================================================
HIGH LEVEL ARCHITECTURE
===============================================================================

                    USER SPACE
                         |
                         |
        +----------------+----------------+
        |                |                |
        v                v                v

      SYSCALL        INTERRUPT       EXCEPTION

        |                |                |
        v                v                v

   system_call    common_interrupt    error_entry

        |                |                |
        +----------------+----------------+
                         |
                         v

                    Kernel C Code

                         |
                         v

                  Return Processing

                         |
                +--------+--------+
                |                 |
                v                 v

              SYSRETQ           IRETQ

                |                 |
                +--------+--------+
                         |
                         v

                    USER SPACE

===============================================================================
STACK FRAME TYPES
===============================================================================

The comments define three important frame types:

------------------------------------------------------------------------------
1. Hardware Interrupt Frame
------------------------------------------------------------------------------

CPU automatically pushes:

    SS
    RSP
    RFLAGS
    CS
    RIP

Layout:

    +-------+
    | SS    |
    +-------+
    | RSP   |
    +-------+
    |FLAGS  |
    +-------+
    | CS    |
    +-------+
    | RIP   |
    +-------+

------------------------------------------------------------------------------
2. Partial Stack Frame
------------------------------------------------------------------------------

Only volatile registers saved.

Fast.

Used for:

    syscall
    interrupt

------------------------------------------------------------------------------
3. Full Stack Frame
------------------------------------------------------------------------------

All registers saved.

Needed for:

    signals
    ptrace
    fork
    execve
    debugging

===============================================================================
SYSTEM CALL PATH
===============================================================================

User:

    read()
    write()
    open()
    fork()

Eventually:

    syscall instruction

CPU executes:

    SYSCALL

Hardware does:

    RCX <- user RIP
    R11 <- user FLAGS

    RIP <- MSR_LSTAR

which enters:

    ENTRY(system_call)

===============================================================================
SYSTEM CALL FLOW
===============================================================================

User Process
      |
      v
syscall instruction
      |
      v
ENTRY(system_call)
      |
      +--> swapgs
      |
      +--> save user rsp
      |
      +--> switch to kernel stack
      |
      +--> SAVE_ARGS
      |
      +--> syscall tracing?
      |
      +--> sys_call_table[nr]
      |
      +--> ret_from_sys_call
      |
      +--> signals?
      +--> reschedule?
      |
      +--> SYSRETQ
      |
      v
User Process

===============================================================================
WHY swapgs ?
===============================================================================

User GS contains:

    TLS
    pthread data

Kernel GS contains:

    PDA
    current task
    kernel stack
    irq stack

Entry:

    swapgs

Switches:

    GS(user)
         ->
    GS(kernel)

Now kernel can access:

    %gs:pda_oldrsp
    %gs:pda_kernelstack

===============================================================================
KERNEL STACK SWITCH
===============================================================================

On syscall CPU DOES NOT switch stack.

Linux must do it manually.

Code:

    movq %rsp,%gs:pda_oldrsp
    movq %gs:pda_kernelstack,%rsp

Meaning:

    Save User Stack
    Switch To Kernel Stack

Before:

    RSP -> User Stack

After:

    RSP -> Kernel Stack

===============================================================================
SYSCALL DISPATCH
===============================================================================

After argument setup:

    call *sys_call_table(,%rax,8)

Equivalent:

    sys_call_table[rax]()

Example:

    rax = __NR_read

becomes:

    sys_read()

===============================================================================
FAST RETURN PATH
===============================================================================

After syscall:

    ret_from_sys_call

Checks:

    pending signals?
    ptrace?
    audit?
    reschedule?

If none:

    SYSRETQ

Fast path:

User
  |
  v
Kernel
  |
  v
SYSRETQ
  |
  v
User

===============================================================================
WHY SOMETIMES IRET?
===============================================================================

Two return methods:

    SYSRETQ
    IRETQ

SYSRETQ
--------

    Fast
    Used normally

IRETQ
------

    Slower
    More robust

Used for:

    signals
    execve
    sigreturn
    ptrace
    modified pt_regs

Reason:

    SYSRET has hardware corner cases.

===============================================================================
SIGNAL HANDLING
===============================================================================

Before returning:

    do_notify_resume()

Checks:

    pending signal?

If yes:

User Return
      |
      v
do_notify_resume()
      |
      v
build signal frame
      |
      v
user signal handler

===============================================================================
INTERRUPT PATH
===============================================================================

Example:

    APIC timer interrupt

CPU receives IRQ:

    vector

Enters:

    common_interrupt

===============================================================================
INTERRUPT FLOW
===============================================================================

Interrupt
     |
     v
common_interrupt
     |
     +--> SAVE_ARGS
     |
     +--> do_IRQ()
     |
     +--> irq_exit()
     |
     +--> schedule?
     |
     +--> signal?
     |
     +--> iretq

===============================================================================
IRQ STACK
===============================================================================

Linux uses dedicated interrupt stack.

Instead of:

Task Stack
    |
    +--> interrupt
    +--> nested interrupt

Linux switches to:

    pda_irqstackptr

Flow:

IRQ
 |
 v
Switch To IRQ Stack
 |
 v
do_IRQ()

Prevents task stack overflow.

===============================================================================
SOFTIRQ PATH
===============================================================================

call_softirq()
      |
      v
__do_softirq()

Used for:

    NET_RX
    NET_TX
    TIMER
    TASKLET

Flow:

Hard IRQ
     |
     v
irq_exit()
     |
     v
call_softirq()
     |
     v
__do_softirq()

===============================================================================
EXCEPTION HANDLING
===============================================================================

Exceptions:

    Page Fault
    General Protection
    Divide Error
    Invalid Opcode
    NMI
    Debug
    Machine Check

All eventually flow through:

    error_entry

===============================================================================
EXCEPTION ENTRY TYPES
===============================================================================

------------------------------------------------------------------------------
zeroentry
------------------------------------------------------------------------------

Exceptions WITHOUT error code.

Examples:

    #DE Divide Error
    #UD Invalid Opcode
    #OF Overflow

Flow:

CPU
 |
 v
zeroentry
 |
 v
error_entry
 |
 v
do_xxx()

------------------------------------------------------------------------------
errorentry
------------------------------------------------------------------------------

Exceptions WITH error code.

Examples:

    #PF Page Fault
    #GP General Protection
    #TS Invalid TSS

Flow:

CPU
 |
 +--> error code
 |
 v
errorentry
 |
 v
error_entry

------------------------------------------------------------------------------
paranoidentry
------------------------------------------------------------------------------

Used for dangerous exceptions:

    NMI
    Double Fault
    Machine Check
    Debug

Can occur anywhere.

Must not trust kernel state.

===============================================================================
PAGE FAULT
===============================================================================

Most important exception.

CPU:

    #PF

Entry:

    page_fault:
        errorentry do_page_fault

Flow:

Memory Access
      |
      v
Page Fault
      |
      v
error_entry
      |
      v
do_page_fault()

===============================================================================
NMI FLOW
===============================================================================

NMI can happen:

    during interrupt
    during page fault
    during scheduler
    anywhere

Flow:

NMI
 |
 v
nmi:
    paranoidentry do_nmi
 |
 v
do_nmi()

Special handling required.

===============================================================================
ERROR ENTRY
===============================================================================

Central exception dispatcher.

Conceptually:

    save_all()
    handler()
    restore_all()
    iretq

Flow:

error_entry
      |
      +--> save registers
      |
      +--> swapgs if needed
      |
      +--> call exception handler
      |
      +--> restore registers
      |
      +--> iretq

===============================================================================
FORK PATH
===============================================================================

New process starts here:

    ret_from_fork

Flow:

do_fork()
      |
      v
context switch
      |
      v
ret_from_fork
      |
      v
schedule_tail()
      |
      v
new process begins

===============================================================================
KERNEL THREAD CREATION
===============================================================================

kernel_thread()
      |
      v
do_fork()
      |
      v
child_rip
      |
      v
fn(arg)
      |
      v
do_exit()

Used for:

    kswapd
    ksoftirqd
    migration
    watchdog

===============================================================================
PREEMPTION CHECK
===============================================================================

Returning from interrupt:

    retint_kernel

Checks:

    preempt_count == 0 ?
    TIF_NEED_RESCHED ?

If yes:

    preempt_schedule_irq()

Flow:

IRQ Return
     |
     v
Need Reschedule?
     |
    Yes
     |
     v
preempt_schedule_irq()
     |
     v
switch task

===============================================================================
SPECIAL RETURN PATHS
===============================================================================

ret_from_sys_call
-----------------

Return from syscall.

Checks:

    signals
    audit
    tracing
    scheduling


retint_check
------------

Return from interrupt.

Checks:

    signals
    reschedule


paranoid_exit
-------------

Return from NMI/Debug/MCE.

Cannot trust normal state.

===============================================================================
MOST IMPORTANT FUNCTIONS TO MASTER
===============================================================================

If learning x86_64 entry code:

1. system_call
2. ret_from_sys_call
3. common_interrupt
4. error_entry
5. page_fault
6. ret_from_fork

Everything else branches from these.

===============================================================================
COMPLETE CONTROL FLOW
===============================================================================

                         USER SPACE
                              |
                              |
        +---------------------+----------------------+
        |                     |                      |
        v                     v                      v

     SYSCALL             INTERRUPT              EXCEPTION

        |                     |                      |
        v                     v                      v

  system_call        common_interrupt        error_entry

        |                     |                      |
        |                     |                      |
        +----------+----------+----------+-----------+
                   |                     |
                   v                     v

              Kernel C Handlers

          sys_*()          do_IRQ()
                           do_page_fault()
                           do_notify_resume()
                           schedule()
                           softirq

                   |
                   v

          Return Processing

          ret_from_sys_call
          retint_check
          paranoid_exit

                   |
          +--------+--------+
          |                 |
          v                 v

       SYSRETQ           IRETQ

          |                 |
          +--------+--------+
                   |
                   v

               USER SPACE

===============================================================================
KEY IDEA
===============================================================================

entry.S is the lowest-level control-transfer code in x86_64 Linux.

Every transition:

    User -> Kernel
    Kernel -> User

passes through this file.

It is simultaneously:

    • Syscall dispatcher
    • Interrupt dispatcher
    • Exception dispatcher
    • Signal gateway
    • Scheduler checkpoint
    • Fork startup path
    • Kernel-thread startup path
    • Return-to-user logic

If start_kernel() is the kernel's "main()",

then entry.S is the kernel's "front door".
===============================================================================
```

