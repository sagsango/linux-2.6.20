```text
===============================================================================
FILE: arch/x86_64/kernel/smpboot.c
PURPOSE: HOW LINUX BOOTS SECONDARY CPUs (APs)
KERNEL: Linux 2.6.x x86-64
===============================================================================

BACKGROUND
===============================================================================

When a PC powers on:

Only ONE CPU starts executing.

That CPU is called:

    BSP = Bootstrap Processor

All other CPUs are sleeping.

These sleeping CPUs are called:

    AP = Application Processor

Example:

    Socket
    |
    +-- CPU0 (BSP)  <-- running
    +-- CPU1 (AP)   <-- sleeping
    +-- CPU2 (AP)   <-- sleeping
    +-- CPU3 (AP)   <-- sleeping

Linux kernel initially runs only on CPU0.

Before SMP can work Linux must wake the APs.

That entire process is implemented here.

===============================================================================
BIG PICTURE
===============================================================================

BIOS
 |
 v
CPU0 starts
 |
 v
Linux kernel boots
 |
 v
smp_prepare_cpus()
 |
 v
__cpu_up(1)
 |
 v
do_boot_cpu()
 |
 v
Send INIT IPI
 |
 v
Send STARTUP IPI
 |
 v
CPU1 wakes up
 |
 v
trampoline executes
 |
 v
start_secondary()
 |
 v
cpu_init()
 |
 v
smp_callin()
 |
 v
CPU1 online

Repeat for CPU2, CPU3, ...

===============================================================================
IMPORTANT TERMINOLOGY
===============================================================================

BSP
    Bootstrap Processor

AP
    Application Processor

APIC
    Advanced Programmable Interrupt Controller

IPI
    Inter Processor Interrupt

INIT IPI
    Reset a CPU

STARTUP IPI (SIPI)
    Tell CPU where boot code is

Trampoline
    Tiny startup code executed by AP

===============================================================================
CPU STATE BEFORE SMP BOOT
===============================================================================

Power on:

CPU0 = running

CPU1 = halted
CPU2 = halted
CPU3 = halted

Only CPU0 executes kernel code.

===============================================================================
IMPORTANT GLOBAL MAPS
===============================================================================

cpu_online_map
----------------

CPUs currently online.

Example:

    CPU0 online
    CPU1 online
    CPU2 offline
    CPU3 offline

bitmap:

    1100

===============================================================================

cpu_possible_map
----------------

CPUs Linux might ever use.

Includes hotplug CPUs.

===============================================================================

cpu_present_map
----------------

CPUs physically present.

===============================================================================

cpu_callout_map
----------------

BSP says:

    "I have started booting this CPU"

===============================================================================

cpu_callin_map
----------------

AP says:

    "I finished initialization"

===============================================================================
BOOT CPU FLOW
===============================================================================

Kernel initialization eventually reaches:

    smp_prepare_cpus()

Purpose:

    Prepare APICs
    Prepare SMP infrastructure

===============================================================================
smp_prepare_boot_cpu()
===============================================================================

Marks BSP online.

CPU0:

    cpu_online_map
    cpu_callout_map

already set.

===============================================================================
CPU BOOT ENTRY
===============================================================================

Main entry:

    __cpu_up(cpu)

Example:

    __cpu_up(1)

Boot CPU1.

===============================================================================
__cpu_up()
===============================================================================

Flow:

Validate CPU
      |
      v
do_boot_cpu()
      |
      v
wait until online
      |
      v
success

===============================================================================
do_boot_cpu()
===============================================================================

MOST IMPORTANT FUNCTION

Responsible for:

    creating idle thread

    allocating PDA

    preparing trampoline

    sending INIT/SIPI

    waiting for AP response

===============================================================================
STEP 1
Create idle thread
===============================================================================

Every CPU must have an idle task.

CPU1 gets:

    idle thread

stored in:

    idle_thread_array[1]

===============================================================================
STEP 2
Setup trampoline
===============================================================================

Function:

    setup_trampoline()

Copies:

    trampoline_data

to:

    SMP_TRAMPOLINE_BASE

Usually low memory:

    0x7000
    0x8000
    etc

===============================================================================

Why?

AP starts in real mode.

Cannot directly execute kernel code.

Needs tiny bootstrap code first.

===============================================================================
TRAMPOLINE FLOW
===============================================================================

AP wakes
 |
 v
Real mode
 |
 v
Trampoline
 |
 v
Protected mode
 |
 v
Long mode
 |
 v
start_secondary()

===============================================================================
STEP 3
Program warm reset vector
===============================================================================

Writes:

0x467
0x469

BIOS reset vector.

This tells AP:

    where trampoline lives

===============================================================================
STEP 4
Send INIT IPI
===============================================================================

Function:

    wakeup_secondary_via_INIT()

===============================================================================

CPU0
 |
 +--> APIC INIT IPI
 |
 +----------------------> CPU1

===============================================================================

INIT means:

    reset processor

CPU enters reset state.

===============================================================================
STEP 5
Send STARTUP IPI
===============================================================================

After INIT:

Send SIPI.

===============================================================================

CPU0
 |
 +--> STARTUP IPI
 |
 +---------------------> CPU1

contains:

    trampoline address

===============================================================================

Intel requires:

    INIT

then

    SIPI

possibly twice.

Kernel sends two SIPIs.

===============================================================================
AP SIDE
===============================================================================

CPU1 receives SIPI.

Starts executing:

    trampoline

===============================================================================

Trampoline does:

    enable protected mode
    setup page tables
    enable long mode
    jump to kernel

Eventually:

    start_secondary()

===============================================================================
start_secondary()
===============================================================================

Main AP entry point.

This is where CPU1 begins life inside Linux.

===============================================================================

Flow:

cpu_init()
     |
     v
smp_callin()
     |
     v
setup APIC timer
     |
     v
TSC sync
     |
     v
mark online
     |
     v
cpu_idle()

===============================================================================
cpu_init()
===============================================================================

From setup64.c

Creates:

    GDT
    TSS
    IST
    GS base
    PDA
    syscall MSRs

Essentially:

    per-CPU architecture initialization.

===============================================================================
smp_callin()
===============================================================================

AP reports:

    "I am alive"

===============================================================================

CPU1
 |
 +--> cpu_set(cpu_callin_map)

===============================================================================

Meanwhile BSP waits.

===============================================================================

CPU0
 |
 +--> wait cpu_callin_map
 |
 +--> CPU1 responded
 |
 +--> success

===============================================================================
CPU BOOT HANDSHAKE
===============================================================================

CPU0                          CPU1
------------------------------------------------

send INIT
send SIPI
                              wake up

                              trampoline

                              start_secondary

set cpu_callout_map

                              wait for callout

                              cpu_init()

                              smp_callin()

                              set cpu_callin_map

CPU0 sees callin

boot complete

===============================================================================
TSC SYNCHRONIZATION
===============================================================================

Very important SMP feature.

Each CPU has:

    TSC

Time Stamp Counter.

Problem:

CPU0 TSC:

    1000000

CPU1 TSC:

    1000800

Not synchronized.

Timekeeping breaks.

===============================================================================

sync_tsc()

tries to synchronize counters.

Uses:

    sync_master()
    get_delta()

===============================================================================

CPU1 repeatedly asks:

    "What time is it on CPU0?"

Computes offset.

Adjusts its TSC.

===============================================================================

Goal:

CPU0 TSC
CPU1 TSC
CPU2 TSC

all nearly identical.

===============================================================================
CPU SIBLING TOPOLOGY
===============================================================================

Function:

    set_cpu_sibling_map()

Builds:

    cpu_sibling_map

and

    cpu_core_map

===============================================================================

Example:

CPU0 thread0
CPU1 thread1

same physical core.

cpu_sibling_map:

    CPU0 <-> CPU1

===============================================================================

Example:

CPU0 CPU1 CPU2 CPU3

same package.

cpu_core_map:

    CPU0 -> {0,1,2,3}

===============================================================================

Scheduler uses this heavily.

===============================================================================
APIC TIMER SETUP
===============================================================================

After AP comes online:

    setup_secondary_APIC_clock()

and

    enable_APIC_timer()

Now scheduler ticks work.

===============================================================================
ONLINE TRANSITION
===============================================================================

Final stage:

cpu_set(cpu_online_map)

per_cpu(cpu_state) = CPU_ONLINE

CPU officially joins system.

===============================================================================

Before:

    CPU exists

After:

    scheduler may schedule tasks

===============================================================================
FINAL STATE
===============================================================================

CPU0
CPU1
CPU2
CPU3

all online.

All have:

    TSS
    GDT
    IDT
    PDA
    idle thread
    APIC timer
    synchronized TSC

===============================================================================
HOTPLUG CPU SUPPORT
===============================================================================

This file also supports:

    CPU hot-add
    CPU hot-remove

Functions:

    __cpu_disable()
    __cpu_die()

===============================================================================

CPU removal flow:

stop interrupts
      |
      v
remove sibling maps
      |
      v
remove online bit
      |
      v
migrate IRQs
      |
      v
CPU dead

===============================================================================
MOST IMPORTANT FUNCTIONS
===============================================================================

smp_prepare_cpus()
    Initial SMP setup

__cpu_up()
    Start one CPU

do_boot_cpu()
    Complete AP boot procedure

wakeup_secondary_via_INIT()
    Send INIT/SIPI sequence

start_secondary()
    AP kernel entry point

smp_callin()
    AP reports alive

sync_tsc()
    Synchronize clocks

set_cpu_sibling_map()
    Build topology

===============================================================================
MENTAL MODEL
===============================================================================

Think of this file as:

    "Linux CPU factory"

CPU starts as:

    powered-off silicon

Then this file:

    allocates idle task
    allocates PDA
    sets up GDT
    sets up TSS
    sends INIT
    sends SIPI
    enters long mode
    synchronizes TSC
    enables APIC timer
    registers topology
    marks CPU online

Result:

    scheduler can run tasks on it.

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/smpboot.c is the master orchestration code that
discovers processors, boots every Application Processor using the
Intel INIT/SIPI protocol, transitions them through trampoline code
into long mode, initializes per-CPU kernel state, synchronizes clocks,
builds topology information, and finally brings each CPU online for
normal Linux scheduling and execution. :contentReference[oaicite:0]{index=0}
===============================================================================
```

