```text
===============================================================================
FILE: arch/x86_64/kernel/time.c
PURPOSE: ENTIRE x86-64 TIMEKEEPING SUBSYSTEM (Linux 2.6)
===============================================================================

This is one of the most important files in early x86-64 Linux.

It answers a deceptively simple question:

    "What time is it?"

Every one of these depends on this file:

    gettimeofday()
    clock_gettime()
    schedule()
    timeout()
    sleep()
    nanosleep()
    jiffies
    timers
    NTP synchronization
    scheduler accounting
    profiling
    RTC updates

This file sits at the center of the kernel's concept of time. :contentReference[oaicite:0]{index=0}

===============================================================================
BIG PICTURE
===============================================================================

Linux must solve two different problems:

1. WALL CLOCK TIME

    Current date/time

    Example:

        2026-06-07 15:20:10

-------------------------------------------------------------------------------

2. ELAPSED TIME

    How much time has passed

    Example:

        task ran 15 ms
        interrupt took 20 us
        timeout expires in 1 second

-------------------------------------------------------------------------------

Hardware provides several clocks:

    TSC
    HPET
    PIT
    PM TIMER
    RTC

Linux combines them into one unified time system.

===============================================================================
HARDWARE TIMERS IN THIS FILE
===============================================================================

        +----------------+
        | RTC CMOS CLOCK |
        +----------------+
                 |
                 v
      Initial wall clock time

------------------------------------------------

        +----------------+
        | PIT 8254       |
        +----------------+
                 |
                 v
      Legacy timer interrupt

------------------------------------------------

        +----------------+
        | HPET           |
        +----------------+
                 |
                 v
      Modern timer source

------------------------------------------------

        +----------------+
        | PM TIMER       |
        +----------------+
                 |
                 v
      ACPI fallback timer

------------------------------------------------

        +----------------+
        | TSC            |
        +----------------+
                 |
                 v
      Fast cycle counter

===============================================================================
WHY MULTIPLE CLOCKS?
===============================================================================

No timer is perfect.

-------------------------------------------------------------------------------
RTC
-------------------------------------------------------------------------------

Pros:

    Keeps real date/time

Cons:

    Slow
    1 second resolution

-------------------------------------------------------------------------------
PIT
-------------------------------------------------------------------------------

Pros:

    Always exists

Cons:

    Low precision

-------------------------------------------------------------------------------
HPET
-------------------------------------------------------------------------------

Pros:

    High precision
    Stable

Cons:

    MMIO read is slow

-------------------------------------------------------------------------------
PM TIMER
-------------------------------------------------------------------------------

Pros:

    Very stable

Cons:

    Port IO is slow

-------------------------------------------------------------------------------
TSC
-------------------------------------------------------------------------------

Pros:

    Extremely fast

Cons:

    Historically not synchronized
    CPU frequency changes affect it

===============================================================================
KERNEL STRATEGY
===============================================================================

Use fastest source possible.

Preferred:

    TSC

Fallback:

    HPET

Fallback:

    PM TIMER

Fallback:

    PIT

===============================================================================
CORE GLOBAL VARIABLES
===============================================================================

cpu_khz

    CPU frequency.

-------------------------------------------------------------------------------

xtime

    Current wall clock.

Conceptually:

    struct timespec xtime

contains:

    seconds
    nanoseconds

-------------------------------------------------------------------------------

jiffies

Kernel tick count.

Example:

    HZ=1000

then:

    jiffies increments every 1 ms

-------------------------------------------------------------------------------

monotonic_base

Monotonic clock base.

Used for:

    clock_gettime(CLOCK_MONOTONIC)

-------------------------------------------------------------------------------

vxtime

Central timing structure.

Contains:

    last TSC value
    last HPET value
    scaling factors
    timing mode

===============================================================================
TIME SOURCES MODES
===============================================================================

vxtime.mode

can be:

    VXTIME_TSC

        use TSC

    VXTIME_HPET

        use HPET

    VXTIME_PMTMR

        use PM timer

===============================================================================
BOOT FLOW
===============================================================================

time_init()
        |
        v
Read CMOS clock
        |
        v
Initialize HPET if present
        |
        v
Calibrate TSC
        |
        v
Setup timer IRQ
        |
        v
Choose timekeeping source
        |
        v
System time starts

===============================================================================
STEP 1: GET INITIAL TIME
===============================================================================

time_init()
        |
        v
get_cmos_time()

RTC provides:

    year
    month
    day
    hour
    minute
    second

Converted into:

    xtime

Now kernel knows:

    current date/time

===============================================================================
get_cmos_time()
===============================================================================

Reads RTC registers.

Example:

    RTC_SECONDS
    RTC_MINUTES
    RTC_HOURS
    RTC_DAY
    RTC_MONTH
    RTC_YEAR

Converts BCD -> binary.

Produces Unix timestamp.

===============================================================================
WHY RTC ONLY USED AT BOOT?
===============================================================================

RTC is extremely slow.

Kernel reads it once.

Then keeps time itself.

Think:

    RTC initializes clock

    TSC/HPET keep clock running

===============================================================================
TSC CALIBRATION
===============================================================================

Need:

    cycles -> time conversion

Question:

    How many TSC cycles per second?

-------------------------------------------------------------------------------

hpet_calibrate_tsc()

or

pit_calibrate_tsc()

Measures:

    TSC ticks
    during known HPET/PIT interval

Calculates:

    cpu_khz

===============================================================================
EXAMPLE
===============================================================================

Suppose:

    3,000,000 cycles
    in 1 ms

Then:

    cpu_khz = 3,000,000

meaning:

    3 GHz CPU

===============================================================================
TIMER INTERRUPT FLOW
===============================================================================

Hardware timer
        |
        v
IRQ0
        |
        v
timer_interrupt()
        |
        v
main_timer_handler()
        |
        v
do_timer()
        |
        v
jiffies++

===============================================================================
MAIN TIMER HANDLER
===============================================================================

This is the heartbeat of Linux.

Runs every timer tick.

Responsibilities:

    update jiffies
    update wall clock
    update monotonic clock
    detect lost ticks
    update scheduler timing
    update RTC occasionally

===============================================================================
LOST TICKS
===============================================================================

Suppose:

    interrupt disabled

for:

    50 ms

Kernel expected:

    5 timer interrupts

received:

    1

Kernel calculates:

    lost = 4

Then:

    do_timer(5)

to catch up.

===============================================================================
handle_lost_ticks()
===============================================================================

Detects timing instability.

Can print:

    Lost X timer ticks

Can even switch:

    TSC -> HPET

if TSC appears unreliable.

Very advanced feature for 2006.

===============================================================================
HOW gettimeofday() WORKS
===============================================================================

User calls:

    gettimeofday()

Kernel:

    do_gettimeofday()

===============================================================================

It does:

    base wall clock
            +
    time since last tick

-------------------------------------------------------------------------------

Formula:

    current time

      = xtime

      + do_gettimeoffset()

===============================================================================
WHY THIS IS NEEDED
===============================================================================

Suppose:

    HZ=100

Tick every:

    10 ms

Without interpolation:

    time changes every 10 ms

Bad.

-------------------------------------------------------------------------------

With TSC interpolation:

    nanosecond-level resolution

between interrupts.

===============================================================================
TSC MODE
===============================================================================

do_gettimeoffset_tsc()

Reads:

    rdtsc

Computes:

    current_tsc - last_tsc

Converts:

    cycles -> microseconds

Returns:

    elapsed time since last tick

===============================================================================
HPET MODE
===============================================================================

do_gettimeoffset_hpet()

Reads:

    HPET counter

Computes:

    counter - last_counter

Returns:

    elapsed time since last tick

===============================================================================
MONOTONIC CLOCK
===============================================================================

monotonic_clock()

Returns:

    nanoseconds since boot

Never goes backwards.

Unaffected by:

    settimeofday()

Used by:

    scheduler
    locking
    performance measurement

===============================================================================
sched_clock()
===============================================================================

Very fast scheduler timestamp.

Uses:

    rdtsc

directly.

Purpose:

    task runtime accounting

Needs speed more than perfect accuracy.

===============================================================================
TIME INITIALIZATION DECISION
===============================================================================

time_init_gtod()

Chooses timing source.

Logic:

    Is TSC trustworthy?

            |
            +-- yes --> TSC

            |
            +-- no --> HPET

            |
            +-- no --> PM TIMER

===============================================================================
unsynchronized_tsc()
===============================================================================

Checks:

    Are multiple CPUs synchronized?

Historically:

    some multi-socket systems

had:

    different TSC values

on different CPUs.

If unsafe:

    don't use TSC

===============================================================================
HPET INITIALIZATION
===============================================================================

hpet_init()

Maps HPET MMIO registers.

Reads:

    HPET_ID

    HPET_PERIOD

Calculates:

    HPET frequency

Configures:

    Timer 0

Enables:

    periodic interrupts

===============================================================================
HPET TIMER FLOW
===============================================================================

HPET Counter
        |
        v
Comparator
        |
        v
Interrupt
        |
        v
timer_interrupt()
        |
        v
main_timer_handler()

===============================================================================
PIT INITIALIZATION
===============================================================================

pit_init()

Programs legacy 8254 PIT.

Still used when:

    HPET unavailable

===============================================================================
CMOS UPDATE
===============================================================================

Kernel periodically updates RTC.

Function:

    set_rtc_mmss()

Purpose:

    keep hardware clock synchronized

with Linux time.

Usually:

    every ~11 minutes

when NTP synchronized.

===============================================================================
NTP INTERACTION
===============================================================================

NTP adjusts:

    xtime

Kernel updates:

    RTC

Therefore:

    RTC remains accurate

across reboots.

===============================================================================
CPU FREQUENCY SCALING
===============================================================================

Huge challenge:

    TSC depends on CPU frequency
    (old systems)

If CPU changes:

    2 GHz -> 1 GHz

timing calculations become wrong.

-------------------------------------------------------------------------------

This file registers:

    cpufreq notifier

Updates:

    cpu_khz
    scaling factors
    loops_per_jiffy

===============================================================================
SUSPEND / RESUME
===============================================================================

Suspend:

    timer_suspend()

stores:

    RTC time

-------------------------------------------------------------------------------

Resume:

    timer_resume()

reads RTC

calculates:

    sleep duration

adds missing time.

Thus:

    wall clock jumps forward correctly.

===============================================================================
SEQLOCKS
===============================================================================

Almost every time read uses:

    xtime_lock

This is a seqlock.

Reader:

    read_seqbegin()

    read clock

    read_seqretry()

If update happened:

    retry

Advantage:

    lockless fast readers

===============================================================================
IMPORTANT DATA FLOW
===============================================================================

                   RTC
                    |
                    v
              get_cmos_time()
                    |
                    v
                 xtime
                    |
                    v
             do_gettimeofday()
                    ^
                    |
         +----------+----------+
         |                     |
         v                     v
      TSC                HPET / PMTMR
         |                     |
         +----------+----------+
                    |
                    v
          do_gettimeoffset()

===============================================================================
INTERRUPT FLOW
===============================================================================

HPET/PIT
    |
    v
IRQ0
    |
    v
timer_interrupt()
    |
    v
main_timer_handler()
    |
    +--> update jiffies
    |
    +--> update xtime
    |
    +--> update monotonic_base
    |
    +--> detect lost ticks
    |
    +--> scheduler accounting
    |
    +--> periodic RTC sync

===============================================================================
WHAT MODERN KERNELS DO DIFFERENTLY
===============================================================================

Linux 2.6:

    PIT
    HPET
    PMTMR
    TSC
    jiffies-centric

-------------------------------------------------------------------------------

Modern Linux:

    Generic Clocksource Framework

    clocksource_tsc
    clocksource_hpet
    clocksource_acpi_pm

    hrtimers

    tickless kernel

    NO_HZ

    clockevents framework

Much cleaner than this file.

===============================================================================
MENTAL MODEL
===============================================================================

Think of this file as:

    "The kernel's master clock manager"

It:

    chooses the best hardware clock
    calibrates it
    services timer interrupts
    tracks wall clock time
    tracks monotonic time
    handles suspend/resume
    handles NTP synchronization
    compensates for lost ticks
    compensates for CPU frequency changes

Everything in Linux that cares about time eventually depends on this file. :contentReference[oaicite:1]{index=1}

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/time.c is the central Linux 2.6 x86-64 timekeeping engine
that initializes RTC/HPET/PIT/TSC hardware timers, calibrates and selects the
best clock source, services periodic timer interrupts, maintains wall-clock and
monotonic time, compensates for lost ticks and CPU frequency changes, and
provides accurate time to the entire kernel and userspace. :contentReference[oaicite:2]{index=2}
===============================================================================
```

