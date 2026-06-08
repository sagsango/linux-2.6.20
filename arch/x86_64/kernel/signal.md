```text
===============================================================================
FILE: arch/x86_64/kernel/signal.c
PURPOSE: SIGNAL DELIVERY AND RETURN ON x86-64
KERNEL: Linux 2.6.x
===============================================================================

BACKGROUND
===============================================================================

Signals are Linux's asynchronous notification mechanism.

Examples:

    Ctrl+C            -> SIGINT
    Segmentation fault-> SIGSEGV
    Kill process      -> SIGTERM
    Debugger step     -> SIGTRAP
    Alarm timer       -> SIGALRM

Unlike a syscall:

    process ---> kernel

a signal is:

    kernel ---> process

The kernel interrupts normal execution and forces userspace
to execute a signal handler.

===============================================================================
WHAT THIS FILE DOES
===============================================================================

This file is the x86-64 implementation of:

    signal delivery
    signal frame construction
    sigreturn
    alternate signal stacks
    syscall restart
    debugger interaction

It answers:

    "How does Linux force a process to run a signal handler?"

===============================================================================
BIG PICTURE
===============================================================================

Userspace

    main()
    {
        ...
    }

Signal arrives

    SIGINT

Kernel:

    save user registers
    build signal frame
    modify RIP
    modify RSP

Return to userspace

Userspace now executes:

    signal_handler()

instead of main()

Later:

    rt_sigreturn()

restores everything

and execution continues exactly where it stopped.

===============================================================================
SIGNAL DELIVERY FLOW
===============================================================================

Interrupt / Exception / Syscall Exit
                |
                v
       do_notify_resume()
                |
                v
           do_signal()
                |
                v
     get_signal_to_deliver()
                |
        signal pending?
           /       \
          no        yes
          |          |
          v          v
     return     handle_signal()
                    |
                    v
             setup_rt_frame()
                    |
                    v
        modify user registers
                    |
                    v
         return to userspace
                    |
                    v
        signal handler runs
                    |
                    v
          rt_sigreturn()
                    |
                    v
       restore original state

===============================================================================
WHY SIGNALS NEED A FRAME
===============================================================================

Suppose process is here:

    main()
    {
        foo();
    }

CPU state:

    RIP = foo()
    RSP = stack

Signal arrives.

Kernel must preserve:

    RIP
    RSP
    registers
    flags
    FPU state
    signal mask

Otherwise process cannot resume.

So Linux creates a:

    signal frame

on userspace stack.

===============================================================================
SIGNAL FRAME
===============================================================================

struct rt_sigframe
{
    pretcode
    ucontext
    siginfo
}

Contains:

    saved registers

    saved signal mask

    saved FPU state

    alternate stack info

    return trampoline

Think:

    miniature process checkpoint

===============================================================================
ALTERNATE SIGNAL STACK
===============================================================================

Function:

    sys_sigaltstack()

Userspace can register:

    sigaltstack()

Example:

    stack overflow happens

Normal stack is corrupted.

Signal handler cannot run there.

Alternative stack:

    +----------------+
    | signal handler |
    +----------------+

Used for:

    SIGSEGV
    crash handlers
    language runtimes

===============================================================================
get_stack()
===============================================================================

Purpose:

    decide where signal frame goes

Normal case:

    use current stack

Special case:

    SA_ONSTACK

then:

    use alternate signal stack

Flow:

Current RSP
      |
      v
SA_ONSTACK?
      |
   yes/no
      |
      v
Choose stack
      |
      v
Allocate signal frame

===============================================================================
do_notify_resume()
===============================================================================

This is where signal delivery begins.

Called when kernel is about to return to userspace.

Path:

syscall
   |
   v
kernel
   |
   v
exit_to_user_mode
   |
   v
do_notify_resume()

Checks:

    pending signals

    single stepping

    restore signal masks

===============================================================================
SINGLE STEP SUPPORT
===============================================================================

Debugger may request:

    single instruction execution

Flag:

    TIF_SINGLESTEP

Code:

    regs->eflags |= TF

TF = Trap Flag

CPU generates:

    #DB

after next instruction.

Used by:

    ptrace
    gdb

===============================================================================
do_signal()
===============================================================================

Core signal delivery function.

This is the heart of the file.

Flow:

do_signal()
    |
    +--> user mode?
    |
    +--> get_signal_to_deliver()
    |
    +--> signal exists?
             |
         yes/no
             |
             v
      handle_signal()

===============================================================================
WHY CHECK user_mode()?
===============================================================================

Signals are delivered when returning to user mode.

Kernel code cannot suddenly run userspace handlers.

Therefore:

    if (!user_mode(regs))
        return;

===============================================================================
get_signal_to_deliver()
===============================================================================

Examines:

    pending queue

    blocked mask

    dispositions

Finds:

    next signal to deliver

Returns:

    signr

and:

    siginfo

===============================================================================
handle_signal()
===============================================================================

This function performs actual delivery.

Responsibilities:

    syscall restart handling

    debugger interaction

    build signal frame

    modify registers

===============================================================================
SYSCALL RESTART LOGIC
===============================================================================

Very important.

Suppose:

    read(fd,...)

blocks.

Signal arrives.

Kernel interrupted syscall.

Question:

Should syscall restart?

Depends on:

    SA_RESTART

and restart type.

Examples:

    -ERESTARTSYS
    -ERESTARTNOINTR
    -ERESTARTNOHAND

Kernel converts them into:

    restart syscall

or:

    return EINTR

===============================================================================
EXAMPLE
===============================================================================

Userspace:

    read()

Kernel:

    blocks

Signal arrives

Kernel:

    read() -> -ERESTARTSYS

Signal handler runs

Return:

    restart read()

or:

    return EINTR

depending on SA_RESTART.

===============================================================================
setup_rt_frame()
===============================================================================

MOST IMPORTANT FUNCTION

This builds signal frame on userspace stack.

Flow:

save registers
save FPU
save sigmask
save stack info
save siginfo
prepare return path

Then modify registers to enter handler.

===============================================================================
SAVE FPU STATE
===============================================================================

If process used floating point:

    used_math()

then:

    save_i387()

Stores:

    x87
    SSE

into signal frame.

Without this:

    floating point state lost.

===============================================================================
setup_sigcontext()
===============================================================================

Stores register snapshot.

Saved:

    RAX
    RBX
    RCX
    RDX

    RSI
    RDI

    RBP
    RSP
    RIP

    R8-R15

    EFLAGS

    CR2

    trap number

    error code

Think:

    complete CPU checkpoint

===============================================================================
SIGNAL FRAME LAYOUT
===============================================================================

Userspace stack

+----------------------+
| saved fpstate        |
+----------------------+
| siginfo              |
+----------------------+
| ucontext             |
+----------------------+
| sigcontext           |
| RIP                  |
| RSP                  |
| RAX                  |
| ...                  |
+----------------------+
| return trampoline    |
+----------------------+

RSP now points here

===============================================================================
HOW KERNEL ENTERS HANDLER
===============================================================================

After frame creation:

regs->rdi = signal number

regs->rsi = &siginfo

regs->rdx = &ucontext

regs->rip = handler

regs->rsp = frame

===============================================================================
EXAMPLE
===============================================================================

Signal handler:

    void handler(int sig,
                 siginfo_t *info,
                 void *ctx)

Kernel sets:

    RDI = sig

    RSI = info

    RDX = ucontext

Then:

    RIP = handler

Userspace resumes.

CPU jumps directly into handler.

===============================================================================
WHY DOES HANDLER LOOK LIKE NORMAL FUNCTION?
===============================================================================

Because kernel manually sets argument registers.

System V ABI:

    arg1 -> RDI
    arg2 -> RSI
    arg3 -> RDX

Signal delivery obeys ABI.

Therefore handler executes like ordinary C call.

===============================================================================
SIGRETURN
===============================================================================

After handler finishes:

    rt_sigreturn()

executes.

This enters kernel.

Function:

    sys_rt_sigreturn()

===============================================================================
WHAT rt_sigreturn DOES
===============================================================================

Reads signal frame.

Restores:

    registers

    stack pointer

    instruction pointer

    signal mask

    flags

    FPU state

After restoration:

process continues exactly where interrupted.

===============================================================================
restore_sigcontext()
===============================================================================

Inverse of setup_sigcontext().

Reads:

    saved registers

Writes:

    pt_regs

Restores:

    RIP
    RSP
    RAX
    ...
    R15

Restores:

    EFLAGS

Restores:

    FPU state

===============================================================================
MAGIC OF SIGNALS
===============================================================================

Before signal:

    RIP = foo()

Signal arrives.

Kernel saves:

    RIP = foo()

Handler runs.

Handler returns.

Kernel restores:

    RIP = foo()

Execution resumes.

Userspace never sees the switch.

===============================================================================
DEBUGGER INTERACTION
===============================================================================

Signal delivery interacts with ptrace.

Notice:

    PT_DTRACE

    TF flag

Trap Flag may be temporarily cleared so debugger
state remains consistent.

Also:

    ptrace_notify(SIGTRAP)

used when single stepping.

===============================================================================
RELATION TO ptrace()
===============================================================================

Debugger:

    gdb

sets:

    TF

CPU executes one instruction.

CPU raises:

    #DB

Kernel sends:

    SIGTRAP

Signal.c delivers SIGTRAP to debugger.

===============================================================================
SIGNAL RETURN PATH
===============================================================================

Userspace handler
        |
        v
rt_sigreturn()
        |
        v
sys_rt_sigreturn()
        |
        v
restore_sigcontext()
        |
        v
restore sigmask
        |
        v
restore FPU
        |
        v
restore RIP/RSP
        |
        v
return to interrupted code

===============================================================================
FULL END-TO-END EXAMPLE
===============================================================================

Userspace:

    while(1)
        sleep(100);

--------------------------------

User presses Ctrl+C

--------------------------------

TTY driver generates:

    SIGINT

--------------------------------

Kernel:

    signal pending

--------------------------------

do_notify_resume()

--------------------------------

do_signal()

--------------------------------

handle_signal()

--------------------------------

setup_rt_frame()

Save:

    RIP
    RSP
    Registers
    FPU
    Sigmask

--------------------------------

Modify:

    RIP = handler

--------------------------------

Return to userspace

--------------------------------

handler(SIGINT)

--------------------------------

handler returns

--------------------------------

rt_sigreturn()

--------------------------------

restore_sigcontext()

--------------------------------

Resume original code

===============================================================================
MENTAL MODEL
===============================================================================

Signal delivery is not:

    function call

Signal delivery is:

    userspace context switch

Linux:

    saves process state

    builds fake stack frame

    rewrites CPU registers

    resumes execution somewhere else

Later:

    restores original CPU state

and continues as if nothing happened.

===============================================================================
RELATION TO OTHER FILES
===============================================================================

signal.c

    delivers signals
    restores context

------------------------------------------------------------

kernel/signal.c

    generic signal logic

------------------------------------------------------------

ptrace.c

    debugger interaction

------------------------------------------------------------

entry.S

    syscall/interrupt entry

------------------------------------------------------------

sched.c

    wakes tasks waiting for signals

------------------------------------------------------------

exit.c

    generates SIGCHLD

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/signal.c implements the complete x86-64 signal
mechanism by saving interrupted CPU state into a userspace signal frame,
rewriting registers so execution enters a signal handler, managing
alternate stacks, debugger interactions, syscall restart semantics, and
later restoring the original execution context through rt_sigreturn(),
making signal delivery behave like a transparent userspace context
switch. :contentReference[oaicite:0]{index=0}
===============================================================================
```

