```text
============================================================
ACPI PM TIMER / PMTMR CLOCKSOURCE HELPER
File: arch/x86_64/kernel/pmtimer.c
Linux 2.6.x Era
============================================================

PURPOSE
------------------------------------------------------------

This file uses the ACPI Power Management Timer, called PMTMR,
as a timing source for the Linux kernel.

It helps Linux answer:

    How much time passed since the last timer interrupt?

    Did we lose timer ticks?

    What is the current time offset inside this jiffy?

    How long should we busy-wait?

This is old x86-64 timekeeping code from before the modern
generic clocksource/clockevent framework became dominant.

============================================================
BACKGROUND: WHY DOES THE KERNEL NEED TIMERS?
============================================================

The kernel needs time for:

    scheduler ticks

    process accounting

    sleep timeouts

    jiffies

    wall clock time

    monotonic time

    delays

    profiling

    networking timers

============================================================
CLASSIC TIMER MODEL
============================================================

Old Linux used periodic timer interrupts.

Example:

    HZ = 100

Means:

    timer interrupt every 10 ms

============================================================

Timer interrupt
      |
      v
jiffies++

============================================================

But between two timer interrupts, time still passes.

So the kernel needs another source to measure:

    partial time within current jiffy

============================================================
WHAT IS PMTMR?
============================================================

PMTMR = ACPI Power Management Timer.

It is a hardware counter exposed by ACPI southbridge logic.

It ticks at:

    3.579545 MHz

That means:

    3.579545 ticks per microsecond

It is usually accessed through an I/O port:

    pmtmr_ioport

============================================================
WHY PMTMR WAS USEFUL
============================================================

TSC was not always reliable on old systems.

Problems with old TSC:

    frequency changed with CPU power management

    unsynchronized between CPUs

    stopped in deep idle states

    wrong under some BIOS/firmware setups

PMTMR was slower to read, but more stable.

============================================================
TSC VS PMTMR
============================================================

TSC:

    very fast

    per-CPU

    historically could be unstable

------------------------------------------------------------

PMTMR:

    slower I/O port read

    chipset/global timer

    stable frequency

    good fallback/reference clock

============================================================
IMPORTANT GLOBALS
============================================================

u32 pmtmr_ioport

    I/O port where PM timer is read.

    Detected during ACPI boot setup.

------------------------------------------------------------

static u32 last_pmtmr_tick

    PM timer value at previous timer interrupt.

------------------------------------------------------------

static u32 offset_delay

    leftover microseconds not enough to form a full jiffy.

============================================================
ACPI_PM_MASK
============================================================

#define ACPI_PM_MASK 0xFFFFFF

PMTMR is treated as a 24-bit counter.

So values wrap around:

    0xFFFFFF -> 0x000000

Masking handles wraparound arithmetic.

============================================================
WHY WRAPAROUND IS OKAY
============================================================

Example:

last = 0xFFFFF0
now  = 0x000010

Raw subtraction underflows.

But:

(now - last) & 0xFFFFFF

gives correct delta.

============================================================
cyc2us()
============================================================

Converts PM timer cycles to microseconds.

PM timer frequency:

    3.579545 cycles / microsecond

So:

    microseconds = cycles / 3.579545

The code approximates:

    cycles * 286 / 1024

Because:

    286 / 1024 ~= 0.2793 us per cycle

============================================================
WHY NOT USE DIVISION?
============================================================

Old kernel time code avoids expensive division.

So it uses:

    multiply
    shift

Code:

    cycles *= 286;
    return cycles >> 10;

============================================================
pmtimer_mark_offset()
============================================================

MOST IMPORTANT FUNCTION

Called from the timer interrupt path.

Purpose:

    calculate elapsed time since last timer interrupt

    update monotonic time

    detect lost ticks

    adjust vxtime.last_tsc

============================================================
FLOW
============================================================

Timer interrupt arrives
        |
        v
pmtimer_mark_offset()
        |
        +--> read PMTMR
        |
        +--> compute delta from last read
        |
        +--> convert cycles to microseconds
        |
        +--> update monotonic_base
        |
        +--> account leftover delay
        |
        +--> compute lost ticks
        |
        +--> update vxtime.last_tsc
        |
        v
return number of lost ticks

============================================================
STEP 1: READ CURRENT PM TIMER
============================================================

tick = inl(pmtmr_ioport)

This reads the hardware counter from I/O port.

============================================================
STEP 2: CALCULATE DELTA
============================================================

delta = cyc2us((tick - last_pmtmr_tick) & ACPI_PM_MASK)

Meaning:

    how many microseconds passed since last timer interrupt?

============================================================
STEP 3: UPDATE LAST TICK
============================================================

last_pmtmr_tick = tick

Now future calls compare against this value.

============================================================
STEP 4: UPDATE MONOTONIC TIME
============================================================

monotonic_base += delta * NSEC_PER_USEC

This advances kernel monotonic time.

============================================================
STEP 5: ADD OFFSET DELAY
============================================================

delta += offset_delay

Why?

Because previous calculation may have had leftover microseconds
that did not equal a full jiffy.

============================================================
STEP 6: DETECT LOST TICKS
============================================================

lost = delta / (USEC_PER_SEC / HZ)

If HZ = 100:

    one tick = 10000 us

Example:

    delta = 30000 us

Then:

    lost = 3

Meaning:

    three timer ticks worth of time passed.

============================================================
STEP 7: SAVE REMAINDER
============================================================

offset_delay = delta % (USEC_PER_SEC / HZ)

Example:

    delta = 23500 us

    one jiffy = 10000 us

    lost = 2

    offset_delay = 3500 us

Those 3500 us are carried into next interrupt.

============================================================
STEP 8: UPDATE vxtime.last_tsc
============================================================

rdtscll(tsc)

vxtime.last_tsc =
    tsc - offset_delay * cpu_khz / 1000

Meaning:

    adjust the TSC baseline so vsyscall/gettimeofday
    can interpolate time correctly between ticks.

============================================================
WHY TSC IS STILL USED?
============================================================

PMTMR is accurate but slow.

TSC is fast.

So old Linux often used:

    PMTMR for correctness

    TSC for fast interpolation

============================================================
pmtimer_mark_offset() RETURN VALUE
============================================================

return lost - 1

Why minus 1?

Because the current timer interrupt already accounts for one tick.

If PMTMR says 3 ticks elapsed:

    current interrupt = 1

    missed extra ticks = 2

So return:

    3 - 1

============================================================
FIRST RUN SPECIAL CASE
============================================================

On first run:

    no reliable previous state

So:

    offset_delay = 0

Also if less than one tick elapsed:

    reset offset_delay

============================================================
pmtimer_wait_tick()
============================================================

Purpose:

    wait until PM timer changes.

Flow:

    read current PMTMR value

    spin until value differs

Used to synchronize with timer tick movement.

============================================================
pmtimer_wait()
============================================================

Busy-wait for a given number of microseconds.

Flow:

    a = pmtimer_wait_tick()

    loop:
        b = inl(pmtmr_ioport)

    until:

        cyc2us(b - a) >= requested_us

============================================================
WHY "ROUNDED UP TO ONE TICK"?
============================================================

The wait starts only after the PM timer changes once.

So it avoids returning too early because of reading near a tick edge.

============================================================
pmtimer_resume()
============================================================

Called after suspend/resume.

Purpose:

    reset last_pmtmr_tick

Why?

During suspend:

    timer state may be stale

On resume:

    old last_pmtmr_tick may produce bogus huge delta

So code does:

    last_pmtmr_tick = inl(pmtmr_ioport)

============================================================
do_gettimeoffset_pm()
============================================================

Purpose:

    return microseconds since last timer interrupt.

Flow:

    offset = last_pmtmr_tick

    now = inl(pmtmr_ioport)

    delta = (now - offset) & ACPI_PM_MASK

    return offset_delay + cyc2us(delta)

============================================================
WHY THIS FUNCTION EXISTS
============================================================

Old gettimeofday logic needed:

    time at last jiffy

plus:

    offset since last jiffy

This function provides that offset.

============================================================
nopmtimer BOOT OPTION
============================================================

Kernel parameter:

    nopmtimer

Effect:

    pmtmr_ioport = 0

Meaning:

    disable use of PM timer.

============================================================
COMPLETE TIMER INTERRUPT FLOW
============================================================

Hardware timer interrupt
        |
        v
timer interrupt handler
        |
        v
pmtimer_mark_offset()
        |
        +--> read PM timer
        +--> compute elapsed microseconds
        +--> update monotonic_base
        +--> detect lost ticks
        +--> update TSC baseline
        |
        v
jiffies accounting continues

============================================================
COMPLETE GETTIMEOFDAY FLOW
============================================================

User calls gettimeofday()
        |
        v
kernel/vsyscall time code
        |
        v
base time at last tick
        |
        v
do_gettimeoffset_pm()
        |
        +--> read PMTMR
        +--> compute partial-jiffy offset
        |
        v
return current time

============================================================
COMPLETE DELAY FLOW
============================================================

Kernel calls:

    pmtimer_wait(100)

        |
        v
read PM timer
        |
        v
spin until 100 us elapsed
        |
        v
return

============================================================
WHY PMTMR IS 24-BIT
============================================================

A 24-bit counter at 3.579545 MHz wraps every:

    2^24 / 3579545 seconds

Approximately:

    4.6 seconds

So the kernel must read it often enough.

Timer interrupts are much more frequent than that.

============================================================
IMPORTANT LIMITATION
============================================================

PMTMR is slow because it uses I/O port reads:

    inl(pmtmr_ioport)

Port I/O is much slower than reading TSC.

That is why PMTMR is used as a stable reference,
not necessarily as the fastest clock.

============================================================
MENTAL MODEL
============================================================

Think of PMTMR as a stable wall clock stopwatch.

Timer interrupt says:

    "A tick happened."

PMTMR says:

    "Actually, this much real time passed."

TSC says:

    "I can cheaply interpolate between timer events."

============================================================
ONE-LINE SUMMARY
============================================================

pmtimer.c uses the ACPI PM Timer, a stable 24-bit 3.579545 MHz
hardware counter read through an I/O port, to measure elapsed
microseconds between timer interrupts, detect lost ticks, update
monotonic time, support busy waits, and provide accurate time
offsets for old x86-64 timekeeping.
============================================================
```

