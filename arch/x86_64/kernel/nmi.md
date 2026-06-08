```text
============================================================
LINUX x86/x86_64 NMI WATCHDOG
File: arch/x86_64/kernel/nmi.c
============================================================

PURPOSE
------------------------------------------------------------

This file implements the classic Linux NMI watchdog.

Goal:

    Detect hard CPU lockups.

Example:

    CPU enters infinite loop
    CPU spins with interrupts disabled
    CPU deadlocks

Normal timer interrupts stop arriving.

But:

    NMI still arrives.

NMI watchdog notices CPU stopped making progress
and triggers panic/backtrace.

============================================================
BACKGROUND:
WHAT IS NMI?
============================================================

NMI

    Non Maskable Interrupt

Unlike normal interrupts:

    cli

cannot disable it.

Normal Interrupt:

    IRQ
      |
      +--> can be masked

------------------------------------------------------------

NMI:

    NMI
      |
      +--> always delivered

============================================================
WHY NMI WATCHDOG EXISTS?
============================================================

Suppose:

CPU0
 |
 +--> spin_lock()
 |
 +--> bug
 |
 +--> infinite loop

Interrupts disabled.

Timer IRQ never executes.

System appears frozen.

NMI still arrives.

Therefore:

    NMI = last line of defense

============================================================
HIGH LEVEL DESIGN
============================================================

Periodic NMI
      |
      v
nmi_watchdog_tick()
      |
      v
Check:

    Is APIC timer count increasing?

YES
    CPU alive

NO
    CPU stuck

      |
      v
die_nmi()
      |
      v
panic/backtrace

============================================================
BOOT FLOW
============================================================

start_kernel()
      |
      v
nmi_watchdog_default()
      |
      v
choose watchdog type
      |
      v
setup_apic_nmi_watchdog()
      |
      v
program performance counter
      |
      v
counter overflow
      |
      v
NMI

============================================================
SUPPORTED MODES
============================================================

NMI_NONE

    Disabled

------------------------------------------------------------

NMI_IO_APIC

    Legacy timer-based watchdog

------------------------------------------------------------

NMI_LOCAL_APIC

    Performance counter overflow NMI

============================================================
WHY PERFORMANCE COUNTERS?
============================================================

Performance counters count:

    cycles
    instructions
    events

Example:

CPU cycles:

    1
    2
    3
    4
    ...

Counter overflow
      |
      v
Generate NMI

Very reliable.

============================================================
IMPORTANT GLOBAL VARIABLES
============================================================

nmi_watchdog

Current watchdog mode.

------------------------------------------------------------

nmi_active

>0

    active

=0

    disabled

<0

    permanently disabled

------------------------------------------------------------

nmi_hz

NMI frequency.

Initially:

    HZ

Later reduced to:

    1 Hz

============================================================
PER-CPU CONTROL BLOCK
============================================================

struct nmi_watchdog_ctlblk

Contains:

enabled

check_bit

perfctr_msr

evntsel_msr

cccr_msr

============================================================
MEANING
============================================================

perfctr_msr

    Performance counter register.

------------------------------------------------------------

evntsel_msr

    Event select register.

------------------------------------------------------------

cccr_msr

    Pentium 4 specific control register.

------------------------------------------------------------

check_bit

    Overflow bit.

============================================================
RESOURCE RESERVATION
============================================================

Problem:

Multiple subsystems may want PMU.

Examples:

    OProfile
    NMI Watchdog
    Perf

Need ownership tracking.

============================================================
perfctr_nmi_owner
============================================================

Per CPU bitmap.

Tracks:

    who owns PMU counters

Functions:

reserve_perfctr_nmi()

release_perfctr_nmi()

============================================================
EXAMPLE
============================================================

OProfile owns counter 0

        |

NMI watchdog asks for counter 0

        |

reserve_perfctr_nmi()

        |

FAIL

============================================================
CPU DETECTION
============================================================

nmi_known_cpu()

AMD:

    Family 15

Intel:

    P4

or

    Architectural PerfMon

============================================================
WATCHDOG SELECTION
============================================================

nmi_watchdog_default()

Known CPU?

        |
        +--> yes
        |       |
        |       v
        |   LOCAL_APIC
        |
        +--> no
                |
                v
            IO_APIC

============================================================
WATCHDOG SELF TEST
============================================================

check_nmi_watchdog()

Purpose:

Verify watchdog really works.

============================================================
FLOW
============================================================

Record:

    old NMI counts

Wait:

    10 watchdog ticks

Compare:

    new NMI counts

============================================================

If count unchanged:

    watchdog broken

Disable watchdog.

============================================================

If counts increased:

    watchdog works

Print:

    OK

============================================================
NMI COUNT
============================================================

Stored:

cpu_pda(cpu)->__nmi_count

Each NMI:

    __nmi_count++

============================================================
AMD K7 WATCHDOG
============================================================

setup_k7_watchdog()

Programs:

MSR_K7_PERFCTR0

MSR_K7_EVNTSEL0

============================================================
EVENT USED
============================================================

K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING

Meaning:

Count CPU cycles.

Overflow periodically.

============================================================
FLOW
============================================================

Performance Counter
        |
        v
Counts cycles
        |
        v
Overflow
        |
        v
NMI

============================================================
PENTIUM 4 WATCHDOG
============================================================

setup_p4_watchdog()

More complicated.

Uses:

CCCR

ESCR

PERFCTR

============================================================
P4 FLOW
============================================================

IQ_COUNTER
      |
      v
Count events
      |
      v
Overflow
      |
      v
PMI
      |
      v
NMI

============================================================
HYPERTHREADING SUPPORT
============================================================

P4 PMU shared.

Need different counters per thread.

HT0:

    PERFCTR0

HT1:

    PERFCTR1

============================================================
ARCHITECTURAL PERFMON
============================================================

Modern Intel CPUs.

setup_intel_arch_watchdog()

Uses:

CPUID leaf 10

============================================================
EVENT USED
============================================================

UNHALTED_CORE_CYCLES

Meaning:

Count cycles while core running.

============================================================
FLOW
============================================================

Counter
    |
    v
Counts cycles
    |
    v
Overflow
    |
    v
NMI

============================================================
WATCHDOG ENABLE FLOW
============================================================

enable_lapic_nmi_watchdog()

        |
        v
on_each_cpu()

        |
        v
setup_apic_nmi_watchdog()

============================================================
setup_apic_nmi_watchdog()
============================================================

Select CPU type.

AMD:

    setup_k7_watchdog()

Intel P4:

    setup_p4_watchdog()

Intel Core:

    setup_intel_arch_watchdog()

============================================================
APIC PROGRAMMING
============================================================

apic_write(
    APIC_LVTPC,
    APIC_DM_NMI
)

Meaning:

Performance counter overflow

        |

becomes

        |

NMI

============================================================
NMI ARRIVAL
============================================================

Hardware
    |
    v
NMI
    |
    v
do_nmi()
    |
    v
default_do_nmi()
    |
    v
nmi_watchdog_tick()

============================================================
do_nmi()
============================================================

nmi_enter()

__nmi_count++

default_do_nmi()

nmi_exit()

============================================================
MAIN WATCHDOG LOGIC
============================================================

nmi_watchdog_tick()

This is the heart of the file.

============================================================
STEP 1
============================================================

notify_die(DIE_NMI)

Allows:

    kprobes
    kgdb
    debuggers

to consume NMI first.

============================================================
STEP 2
============================================================

Read:

    apic_timer_irqs

Stored:

    sum

============================================================
WHAT IS apic_timer_irqs?
============================================================

Counts LAPIC timer interrupts.

Healthy CPU:

100
101
102
103

============================================================

Locked CPU:

100
100
100
100

============================================================
STEP 3
============================================================

Compare:

last_irq_sum

vs

current sum

============================================================

Changed?

YES

    CPU alive

NO

    CPU may be locked

============================================================
STEP 4
============================================================

Increment:

alert_counter

============================================================

5 seconds pass?

YES

    LOCKUP DETECTED

============================================================
STEP 5
============================================================

die_nmi(
    "LOCKUP"
)

May:

    panic

or

    dump stack

============================================================
SOFTLOCKUP INTERACTION
============================================================

touch_nmi_watchdog()

Resets watchdog state.

Called from:

    scheduler
    kernel code

Meaning:

    "I'm alive."

============================================================
MCE INTERACTION
============================================================

if (atomic_read(&mce_entry) > 0)

Meaning:

CPU currently handling:

    Machine Check Exception

Don't report false lockup.

============================================================
BACKTRACE SUPPORT
============================================================

__trigger_all_cpu_backtrace()

Sets:

backtrace_mask

============================================================

Next NMI:

CPU sees:

    cpu in backtrace_mask

============================================================

Then:

dump_stack()

============================================================
FLOW
============================================================

Admin requests backtrace
        |
        v
Set mask
        |
        v
NMI arrives
        |
        v
dump_stack()
        |
        v
print CPU backtrace

============================================================
UNKNOWN NMI
============================================================

Some NMI sources:

    watchdog
    parity error
    external NMI button
    hardware faults

Unknown source?

============================================================

unknown_nmi_panic_callback()

Reads:

    get_nmi_reason()

Prints:

    NMI received for unknown reason

Then:

    panic

============================================================
POWER MANAGEMENT
============================================================

Suspend:

lapic_nmi_suspend()

        |
        v
stop_apic_nmi_watchdog()

============================================================

Resume:

lapic_nmi_resume()

        |
        v
setup_apic_nmi_watchdog()

============================================================
WHY STOP DURING SUSPEND?
============================================================

PMU state lost.

Counter stops.

Would generate false watchdog failures.

============================================================
FULL WATCHDOG FLOW
============================================================

Boot
 |
 v
setup_apic_nmi_watchdog()
 |
 +--> program PMU
 |
 +--> APIC_LVTPC = NMI
 |
 v
Counter running
 |
 v
Counter overflow
 |
 v
NMI
 |
 v
do_nmi()
 |
 v
nmi_watchdog_tick()
 |
 +--> LAPIC timer advancing?
 |       |
 |       +--> yes
 |       |      CPU healthy
 |       |
 |       +--> no
 |              |
 |              +--> alert_counter++
 |              |
 |              +--> 5 seconds?
 |                        |
 |                        +--> die_nmi()
 |
 v
Return

============================================================
RELATION TO MODERN KERNELS
============================================================

Linux 2.6:

    NMI watchdog built directly
    on PMU programming.

Modern Linux:

    perf subsystem
          |
          v
    hardlockup detector

Much cleaner.

But concept is identical:

    PMU overflow
          |
          v
    NMI
          |
          v
    detect hard lockups

============================================================
MENTAL MODEL
============================================================

Think of this file as:

    Hardware heartbeat monitor

Every CPU must periodically prove:

    "I am still executing"

If heartbeat disappears:

    NMI watchdog panics.

============================================================
ONE-LINE SUMMARY
============================================================

nmi.c implements the classic x86 NMI watchdog by
programming CPU performance counters to periodically
generate NMIs, then using those NMIs to verify that each
CPU's LAPIC timer and execution progress are still advancing,
allowing Linux to detect hard CPU lockups even when normal
interrupts are completely blocked.
============================================================
```

Based on the uploaded `nmi.c` source. 

