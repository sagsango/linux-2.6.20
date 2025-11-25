# all the traps (symbols are not there but see arch/x86_64/kernel/traps.c:trap_init())
+-------+------------------------------+------------------------------------------------+
| Vec # | Symbolic Name                | When / Why It Happens                          |
+-------+------------------------------+------------------------------------------------+
|  0    | DIVIDE_ERROR_VECTOR          | Divide or remainder (DIV/IDIV) by zero.        |   <--------<<<
|  1    | DEBUG_VECTOR                 | Single-step, hardware breakpoint, or DR6 hit.  |
|  2    | NMI_VECTOR                   | Non-Maskable Interrupt from chipset/watchdog.  |
|  3    | INT3_VECTOR (BREAKPOINT)     | INT3 instruction used by debuggers.            |   <--------<<<
|  4    | OVERFLOW_VECTOR              | INTO instruction executed with OF=1.           |
|  5    | BOUNDS_VECTOR                | BOUND instruction detects out-of-range index.  |
|  6    | INVALID_OP_VECTOR            | Undefined or illegal opcode executed.          |
|  7    | DEVICE_NOT_AVAILABLE_VECTOR  | FPU/SSE instruction when TS bit set (lazy FPU).|
|  8    | DOUBLE_FAULT_VECTOR          | Exception occurs while handling another fault. |   <---------<<<
|  9    | COPROCESSOR_SEGMENT_OVERRUN_VECTOR | Legacy x87 segment overrun (obsolete).   |
| 10    | INVALID_TSS_VECTOR           | Task switch to invalid or corrupt TSS.         |
| 11    | SEGMENT_NOT_PRESENT_VECTOR   | Load/use of a not-present segment descriptor.  |
| 12    | STACK_SEGMENT_VECTOR         | Stack-segment fault (bad SS selector, limit).  |
| 13    | GENERAL_PROTECTION_VECTOR    | Privilege, descriptor, or protection violation.|
| 14    | PAGE_FAULT_VECTOR            | Memory access to unmapped or protected page.   |   <---------<<<
| 15    | SPURIOUS_INTERRUPT_VECTOR    | Spurious hardware interrupt (usually IRQ7).    |
| 16    | COPROCESSOR_ERROR_VECTOR     | x87 FPU exception signaled by ES bit.          |
| 17    | ALIGNMENT_CHECK_VECTOR       | Unaligned memory access with AC flag set.      |
| 18    | MACHINE_CHECK_VECTOR         | CPU hardware error (ECC, parity, thermal).     |
| 19    | SIMD_COPROCESSOR_ERROR_VECTOR| SSE/SIMD floating-point exception (#XM).       |
|20–31  | RESERVED_TRAP_VECTORS        | Reserved by Intel; undefined if triggered.     |
+-------+------------------------------+------------------------------------------------+



# standard Linux signal delevered to userspace (defined in include/asm-x86_64/signal.h)
+--------+-----------+------------------------------------------------------------+
| Number | Name      | When / Why It Is Delivered                                 |
+--------+-----------+------------------------------------------------------------+
|   1    | SIGHUP    | Terminal line hangup or controlling terminal closed.       |
|   2    | SIGINT    | Interrupt from keyboard (Ctrl-C).                          |
|   3    | SIGQUIT   | Quit from keyboard (Ctrl-\) with core dump.                |
|   4    | SIGILL    | Illegal instruction (invalid or privileged opcode).        |
|   5    | SIGTRAP   | Trace or breakpoint trap (debugger event).                 |
|   6    | SIGABRT   | Abort signal from abort(3) or internal consistency check.  |
|   7    | SIGBUS    | Bus error: invalid physical memory access or alignment.    |
|   8    | SIGFPE    | Arithmetic error (divide-by-zero, overflow, etc.).         |
|   9    | SIGKILL   | Kill signal (cannot be caught or ignored).                 |
|  10    | SIGUSR1   | User-defined signal #1.                                    |
|  11    | SIGSEGV   | Invalid memory reference (segmentation fault).             |     <---------<<
|  12    | SIGUSR2   | User-defined signal #2.                                    |
|  13    | SIGPIPE   | Write to a pipe or socket with no reader.                  |
|  14    | SIGALRM   | Alarm clock timer expired (from alarm(2)).                 |
|  15    | SIGTERM   | Termination request (default polite way to kill).          |
|  16    | SIGSTKFLT | Coprocessor or stack fault (obsolete on most systems).     |
|  17    | SIGCHLD   | Child process stopped or terminated.                       |
|  18    | SIGCONT   | Continue a stopped process (opposite of SIGSTOP).          |
|  19    | SIGSTOP   | Stop process execution (cannot be caught or ignored).      |
|  20    | SIGTSTP   | Stop from terminal (Ctrl-Z).                               |
|  21    | SIGTTIN   | Background process read from controlling terminal.         |
|  22    | SIGTTOU   | Background process write to controlling terminal.          |
|  23    | SIGURG    | Urgent condition on socket (out-of-band data).             |
|  24    | SIGXCPU   | CPU time limit exceeded (setrlimit RLIMIT_CPU).            |
|  25    | SIGXFSZ   | File size limit exceeded (setrlimit RLIMIT_FSIZE).         |
|  26    | SIGVTALRM | Virtual timer expired (user CPU time, ITIMER_VIRTUAL).     |
|  27    | SIGPROF   | Profiling timer expired (user+kernel CPU time).            |
|  28    | SIGWINCH  | Window size change (terminal resized).                     |
|  29    | SIGIO     | Asynchronous I/O event (fcntl O_ASYNC) / I/O possible.     |
|  30    | SIGPWR    | Power failure or UPS event.                                |
|  31    | SIGSYS    | Bad system call (invalid syscall number).                  |
|  31    | SIGUNUSED | Legacy alias for SIGSYS (unused).                          |
| 32–63  | SIGRTMIN–SIGRTMAX | POSIX real-time signals, queued and prioritized.   |
+--------+-----------+------------------------------------------------------------+



# Exception → Signal Mapping (summary table)
+--------------------------+------------------------+--------------------------+
| Trap                     | Signal                 | Typical reason           |
| ------------------------ | ---------------------- | ------------------------ |
| #DE Divide Error         | SIGFPE                 | divide by zero           |
| #DB Debug                | SIGTRAP                | single step, watchpoint  |
| #BP Breakpoint           | SIGTRAP                | INT3                     |
| #OF Overflow             | SIGSEGV                | INTO                     |
| #BR Bound Range          | SIGSEGV                | BOUND check              |
| #UD Invalid Opcode       | SIGILL                 | illegal instruction      |
| #NM Device Not Available | SIGSEGV                | lazy FPU                 |
| #DF Double Fault         | SIGSEGV (kernel panic) | nested fault             |
| #TS Invalid TSS          | SIGSEGV                | bad TSS                  |
| #NP Segment Not Present  | SIGBUS                 | bad segment              |
| #SS Stack Fault          | SIGBUS                 | stack error              |
| #GP General Protection   | SIGSEGV                | invalid memory/privilege |
| #PF Page Fault           | SIGSEGV                | bad address              |
| #MF x87 FP Error         | SIGFPE                 | FP exception             |
| #AC Alignment Check      | SIGBUS                 | unaligned memory         |
| #MC Machine Check        | SIGSEGV                | hardware error           |
| #XM SIMD FP Error        | SIGFPE                 | SSE exception            |
+--------------------------+------------------------+--------------------------+



# How signals emerge from traps
entry.S: invalid_op:
    zeroentry do_invalid_op
→ do_invalid_op():
    DO_ERROR_INFO(6, SIGILL, ...)
→ do_trap():
    force_sig_info(SIGILL, &info, current)
→ signal_pending()
→ do_notify_resume() → do_signal() → handle_signal()
→ build frame → return to user via IRETQ → execute signal handler

# trap flow (entry.S -> trap.c ~~> signal)
CPU exception
  ↓
IDT entry → entry.S handler (e.g., page_fault:)
  ↓
SAVE_ALL → call do_page_fault() / do_general_protection() / etc.
  ↓
[ here: traps.c C functions ]
  ↓
  - decide if user vs kernel
  - maybe fix up via exception table
  - else send a signal or call die()
  ↓
signal delivered or kernel panic


# trap in trap.c
traps.c
├── Forward declarations for handlers (divide_error, page_fault, etc.)
├── “die” notifier chain setup (register_die_notifier)
├── Stack / backtrace printing utilities (dump_trace, show_stack, etc.)
├── Oops handling core (oops_begin, oops_end, die, __die, die_nmi)
├── Generic trap dispatcher (do_trap)
│   ├── Macros DO_ERROR / DO_ERROR_INFO → define do_xxx handlers
│   └── Called from entry.S stubs
├── Special cases:
│   ├── do_double_fault()
│   ├── do_general_protection()
│   ├── do_debug(), do_int3(), default_do_nmi()
│   ├── do_coprocessor_error(), do_simd_coprocessor_error()
│   ├── math_state_restore()
├── IDT setup (trap_init)
└── Early parameter parsing (oops=, kstack=)

