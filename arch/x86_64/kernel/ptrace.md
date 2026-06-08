```text
============================================================
x86-64 PTRACE IMPLEMENTATION
File: arch/x86_64/kernel/ptrace.c
Linux 2.6.x Era
============================================================

PURPOSE
------------------------------------------------------------

This file implements:

    debugging

    gdb support

    strace support

    register inspection

    register modification

    memory inspection

    memory modification

    hardware breakpoints

    single stepping

    syscall tracing

for x86-64 Linux. :contentReference[oaicite:0]{index=0}

============================================================
WHAT IS PTRACE?
============================================================

ptrace() means:

    Process Trace

One process (debugger)

controls

another process (tracee).

============================================================

GDB
 |
 v
ptrace()
 |
 v
Kernel
 |
 v
Target Process

============================================================

Examples:

gdb

strace

ltrace

rr

crash debuggers

============================================================
BIG PICTURE
============================================================

Debugger
     |
     +--> Read memory
     |
     +--> Write memory
     |
     +--> Read registers
     |
     +--> Change RIP
     |
     +--> Set breakpoints
     |
     +--> Single step
     |
     +--> Trace syscalls
     |
     v
Kernel ptrace.c

============================================================
HOW GDB "NEXT" WORKS
============================================================

User:

    next

============================================================

GDB
 |
 v
PTRACE_SINGLESTEP
 |
 v
Kernel sets TF
 |
 v
CPU executes ONE instruction
 |
 v
#DB exception
 |
 v
SIGTRAP
 |
 v
GDB wakes up

============================================================
PART 1
TRAP FLAG (TF)
============================================================

Most important concept.

============================================================

RFLAGS bit 8

============================================================

#define TRAP_FLAG 0x100

============================================================

When TF=1:

CPU executes

ONE instruction

then generates

#DB

(Debug Exception)

============================================================

CPU
 |
 v
Instruction
 |
 v
#DB

============================================================
THIS IS HARDWARE SINGLE STEPPING
============================================================

No INT3 required.

No code patching.

Pure CPU feature.

============================================================
PART 2
set_singlestep()
============================================================

Used by:

PTRACE_SINGLESTEP

============================================================

Flow

============================================================

set TIF_SINGLESTEP

      |

set TF bit

      |

resume task

      |

execute 1 instruction

      |

#DB

      |

SIGTRAP

============================================================

Code:

regs->eflags |= TRAP_FLAG

============================================================
WHY TIF_SINGLESTEP TOO?
============================================================

Because Linux wants stepping through:

    user mode

    syscalls

    kernel transitions

============================================================

TF alone isn't enough.

============================================================
PART 3
THE CLEVER TF CHECK
============================================================

Function:

is_setting_trap_flag()

============================================================

Question:

What if the instruction itself changes TF?

============================================================

Example:

popf

iret

============================================================

These instructions modify flags.

============================================================

Kernel scans future instruction bytes:

============================================================

RIP
 |
 +--> opcode
 |
 +--> popf ?
 |
 +--> iret ?
 |
 +--> prefixes ?

============================================================

If instruction modifies TF itself:

Kernel behaves differently.

============================================================
PART 4
clear_singlestep()
============================================================

Removes:

    TIF_SINGLESTEP

and maybe:

    TF

============================================================

Only clears TF if kernel originally set it.

============================================================

Tracked using:

PT_DTRACE

============================================================
PART 5
MEMORY ACCESS
============================================================

Debugger wants:

============================================================

read memory

============================================================

GDB:

x/10x address

============================================================

Uses:

PTRACE_PEEKDATA

PTRACE_PEEKTEXT

============================================================

Flow

============================================================

Debugger
 |
 v
ptrace(PTRACE_PEEKDATA)
 |
 v
arch_ptrace()
 |
 v
access_process_vm()
 |
 v
copy target memory
 |
 v
return data

============================================================
WRITE MEMORY
============================================================

Used for:

============================================================

patch code

write variables

insert breakpoints

============================================================

PTRACE_POKEDATA

PTRACE_POKETEXT

============================================================

Flow

============================================================

Debugger
 |
 v
ptrace(POKETEXT)
 |
 v
access_process_vm()
 |
 v
write target memory

============================================================
PART 6
REGISTER ACCESS
============================================================

GDB needs:

============================================================

RAX

RBX

RCX

RIP

RSP

RFLAGS

============================================================

Kernel stores these inside:

pt_regs

============================================================

Functions:

getreg()

putreg()

============================================================
GETREG FLOW
============================================================

Debugger
 |
 v
PTRACE_GETREGS
 |
 v
getreg()
 |
 v
read pt_regs
 |
 v
return register

============================================================
SETREG FLOW
============================================================

Debugger
 |
 v
PTRACE_SETREGS
 |
 v
putreg()
 |
 v
modify pt_regs

============================================================
WHY MODIFY RIP?
============================================================

GDB command:

jump

============================================================

Changes:

RIP

============================================================

Next resume executes elsewhere.

============================================================
PART 7
FS / GS SPECIAL HANDLING
============================================================

64-bit Linux uses:

FS base

GS base

============================================================

Thread Local Storage

============================================================

Kernel stores:

thread.fs

thread.gs

============================================================

PTRACE can read/write:

FS_BASE

GS_BASE

============================================================
PART 8
HARDWARE BREAKPOINTS
============================================================

x86 CPUs contain:

DR0
DR1
DR2
DR3

============================================================

Breakpoint addresses.

============================================================

DR7

Control register.

============================================================

Example:

DR0 = 0x400000

============================================================

CPU automatically traps when address hit.

============================================================
FLOW
============================================================

Debugger
 |
 v
PTRACE_POKEUSR
 |
 v
write DR0
 |
 v
write DR7
 |
 v
resume task
 |
 v
CPU executes
 |
 v
address hit
 |
 v
#DB

============================================================
WHY USE HARDWARE BREAKPOINTS?
============================================================

Unlike INT3:

============================================================

No code modification

============================================================

Works on:

read access

write access

execute access

============================================================
PART 9
PTRACE_SINGLESTEP
============================================================

Request:

PTRACE_SINGLESTEP

============================================================

Flow

============================================================

GDB
 |
 v
PTRACE_SINGLESTEP
 |
 v
set_singlestep()
 |
 v
TF=1
 |
 v
wake_up_process()
 |
 v
run one instruction
 |
 v
#DB
 |
 v
SIGTRAP
 |
 v
GDB

============================================================
PART 10
SYSCALL TRACING
============================================================

Used by:

strace

============================================================

Request:

PTRACE_SYSCALL

============================================================

Kernel sets:

TIF_SYSCALL_TRACE

============================================================
ENTRY FLOW
============================================================

User syscall
 |
 v
syscall entry
 |
 v
syscall_trace_enter()
 |
 v
SIGTRAP
 |
 v
strace

============================================================
EXIT FLOW
============================================================

syscall completes
 |
 v
syscall_trace_leave()
 |
 v
SIGTRAP
 |
 v
strace

============================================================

Thus strace sees:

enter

leave

for every syscall.

============================================================
EXAMPLE
============================================================

write(1,"hello",5)

============================================================

Entry stop:

RAX = SYS_write

RDI = 1

RSI = ptr

RDX = 5

============================================================

Resume

============================================================

Exit stop:

RAX = return value

============================================================
PART 11
syscall_trace_enter()
============================================================

Called before syscall executes.

============================================================

Performs:

1) seccomp check

2) ptrace syscall stop

3) audit logging

============================================================

Flow

============================================================

syscall
 |
 v
secure_computing()
 |
 v
ptrace?
 |
 v
audit?
 |
 v
real syscall

============================================================
PART 12
syscall_trace_leave()
============================================================

Runs after syscall.

============================================================

Used by:

strace

audit

debuggers

============================================================
PART 13
ARCH_PRCTL SUPPORT
============================================================

Special ptrace request:

PTRACE_ARCH_PRCTL

============================================================

Allows debugger to:

read FS base

write FS base

read GS base

write GS base

============================================================

Very useful for TLS debugging.

============================================================
PART 14
convert_rip_to_linear()
============================================================

Interesting legacy helper.

============================================================

Used when tracing:

32-bit

LDT-based

segmented code

============================================================

Converts:

CS:RIP

into

Linear Address

============================================================

Needed because old x86 segmentation
can give non-zero segment bases.

============================================================
PART 15
WHY PTRACE TOUCHES KERNEL STACK?
============================================================

Functions:

get_stack_long()

put_stack_long()

============================================================

Access saved registers directly from:

task kernel stack

============================================================

Remember:

pt_regs lives on kernel stack.

============================================================

Task Stack

+--------------------+
| pt_regs            |
| RIP                |
| RSP                |
| RAX                |
| RFLAGS             |
+--------------------+

============================================================
PART 16
CONTINUE
============================================================

Request:

PTRACE_CONT

============================================================

Flow

============================================================

Debugger
 |
 v
CONT
 |
 v
clear singlestep
 |
 v
deliver signal
 |
 v
wake task
 |
 v
run normally

============================================================
PART 17
DETACH
============================================================

Request:

PTRACE_DETACH

============================================================

Debugger stops controlling task.

============================================================

Task resumes normally.

============================================================
PART 18
KILL
============================================================

Request:

PTRACE_KILL

============================================================

Kernel:

child->exit_code = SIGKILL

wake_up_process()

============================================================

Target dies.

============================================================
COMPLETE SINGLE STEP FLOW
============================================================

GDB
 |
 v
PTRACE_SINGLESTEP
 |
 v
set TF
 |
 v
task runs
 |
 v
1 instruction
 |
 v
CPU raises #DB
 |
 v
do_debug()
 |
 v
SIGTRAP
 |
 v
task stops
 |
 v
GDB notified

============================================================
COMPLETE STRACE FLOW
============================================================

strace
 |
 v
PTRACE_SYSCALL
 |
 v
task runs
 |
 v
syscall entry
 |
 v
SIGTRAP
 |
 v
strace prints syscall
 |
 v
resume
 |
 v
syscall executes
 |
 v
syscall exit
 |
 v
SIGTRAP
 |
 v
strace prints return value

============================================================
RELATION TO KPROBES
============================================================

You asked earlier whether kprobes still uses INT3.

Yes.

============================================================

KPROBE

patch instruction

INT3

#BP

============================================================

PTRACE SINGLESTEP

set TF

#DB

============================================================

Hardware breakpoints

DR0-DR7

#DB

============================================================

Three different debugging mechanisms.

============================================================
MENTAL MODEL
============================================================

ptrace.c is the x86-64 debugger backend.

GDB
strace
rr
ltrace

all eventually rely on this file.

============================================================

Memory Access
       |
Registers
       |
Single Step
       |
Syscall Trace
       |
Hardware Breakpoints
       |
TLS Inspection

============================================================

all converge into:

arch_ptrace()

============================================================
ONE-LINE SUMMARY
============================================================

ptrace.c is the x86-64 architecture-specific debugging engine that
implements register access, memory access, single stepping through
the Trap Flag (TF), syscall tracing, hardware breakpoints via DR0-DR7,
TLS inspection, and the low-level mechanisms used by GDB, strace,
and other debuggers. :contentReference[oaicite:1]{index=1}
============================================================
```

