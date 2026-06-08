===============================================================================
LOCAL APIC / IO APIC / APIC TIMER
File: arch/x86_64/kernel/apic.c (Linux 2.6.20)
===============================================================================

WHAT PROBLEM DOES THIS FILE SOLVE?
==================================

Old PCs used:

                   8259 PIC
                        |
                        v
                      CPU

Problems:

    - Only 16 IRQs
    - No SMP support
    - No per-CPU interrupts
    - No IPIs
    - No local timers

Modern systems use APIC architecture.


===============================================================================
APIC ARCHITECTURE
===============================================================================

                    Hardware IRQs
                           |
                           v

                    +---------------+
                    |   IO APIC     |
                    +-------+-------+
                            |
        +-------------------+-------------------+
        |                                       |
        v                                       v

+-------------------+               +-------------------+
| CPU0              |               | CPU1              |
|                   |               |                   |
| Local APIC        |<----IPI-----> | Local APIC        |
| LAPIC Timer       |               | LAPIC Timer       |
+-------------------+               +-------------------+


Responsibilities:

IO APIC
--------
    Receives device interrupts.

LAPIC
-----
    Receives interrupts for one CPU.

APIC Timer
----------
    Generates scheduler ticks.

IPI
---
    CPU-to-CPU interrupts.


===============================================================================
BOOT FLOW
===============================================================================

start_kernel()
      |
      v
APIC_init_uniprocessor()
      |
      +--> verify_local_APIC()
      |
      +--> init_apic_mappings()
      |
      +--> setup_local_APIC()
      |
      +--> calibrate_APIC_clock()
      |
      +--> setup_APIC_timer()
      |
      v
System Running


===============================================================================
PART 1 : MAP LAPIC / IOAPIC MMIO
===============================================================================

Function:

    init_apic_mappings()

Background
----------

LAPIC is memory mapped hardware.

Typical physical addresses:

    LAPIC  = 0xFEE00000
    IOAPIC = 0xFEC00000


Physical Memory:

+------------------------------------------------+
|                                                |
| 0xFEC00000 --> IO APIC Registers               |
|                                                |
| 0xFEE00000 --> Local APIC Registers            |
|                                                |
+------------------------------------------------+


Linux creates virtual mappings:

set_fixmap_nocache(FIX_APIC_BASE,...)

Result:

Kernel Virtual Address
          |
          v
LAPIC Registers


Flow:

init_apic_mappings()
       |
       +--> map LAPIC page
       |
       +--> reserve LAPIC resource
       |
       +--> map IOAPIC pages
       |
       +--> reserve IOAPIC resources


===============================================================================
PART 2 : VERIFY LAPIC EXISTS
===============================================================================

Function:

    verify_local_APIC()

Purpose:

Verify that LAPIC hardware is real.

Checks:

1. APIC version register

        APIC_LVR

2. APIC ID register

        APIC_ID

3. LVT registers

        LVT0
        LVT1


Flow:

Read Version
      |
      +--> Looks valid?
              |
              +--> No -> Not APIC
              |
              +--> Yes
                      |
                      v
                Read APIC ID
                      |
                      +--> Valid?
                              |
                              +--> No
                              |
                              +--> Yes
                                      |
                                      v
                               LAPIC Exists


===============================================================================
PART 3 : LAPIC INITIALIZATION
===============================================================================

Function:

    setup_local_APIC()

Purpose:

Enable LAPIC and configure interrupt delivery.

Flow:

setup_local_APIC()
      |
      +--> setup LDR
      |
      +--> setup DFR
      |
      +--> clear pending ISR bits
      |
      +--> enable APIC
      |
      +--> setup LVT0
      |
      +--> setup LVT1
      |
      +--> setup Error Vector
      |
      v
LAPIC Ready


===============================================================================
IMPORTANT LAPIC REGISTERS
===============================================================================

APIC_ID
--------
CPU's APIC ID

Example:

    CPU0 -> APIC ID 0
    CPU1 -> APIC ID 1


APIC_SPIV
---------
Spurious Interrupt Vector Register

Controls:

    - APIC Enable
    - Spurious Vector


APIC_LVT0
---------
LINT0

Used for:

    External Interrupts


APIC_LVT1
---------
LINT1

Used for:

    NMI


APIC_LVTERR
-----------
Error Interrupt


APIC_ISR
---------
In Service Register

Tracks interrupts currently being serviced.


===============================================================================
ENABLING LAPIC
===============================================================================

Register:

    APIC_SPIV

Bit:

    APIC_SPIV_APIC_ENABLED

Code Logic:

    Read SPIV
    Set Enable Bit
    Write SPIV

Result:

CPU can now receive APIC interrupts.


Before:

    Interrupts ignored

After:

    LAPIC active


===============================================================================
LVT0 CONFIGURATION
===============================================================================

Purpose:

Connect external interrupts.

Configured as:

    APIC_DM_EXTINT


Flow:

8259 PIC
     |
     v
LINT0
     |
     v
LAPIC
     |
     v
CPU


Only BSP normally enables this.


===============================================================================
LVT1 CONFIGURATION
===============================================================================

Purpose:

NMI Delivery

Configured:

    APIC_DM_NMI


Flow:

NMI Source
      |
      v
LINT1
      |
      v
CPU


===============================================================================
ERROR INTERRUPTS
===============================================================================

Register:

    APIC_LVTERR

Vector:

    ERROR_APIC_VECTOR

Purpose:

Receive LAPIC hardware errors.


Examples:

    Illegal Vector
    Send Error
    Receive Error


Handler:

    smp_error_interrupt()


===============================================================================
PART 4 : APIC TIMER
===============================================================================

BACKGROUND
==========

Every CPU has its own timer.

Old systems:

    PIT
      |
      v
    One global timer

Modern systems:

CPU0
  |
  +--> LAPIC Timer

CPU1
  |
  +--> LAPIC Timer


Advantages:

    Per CPU timing
    Better SMP scalability


===============================================================================
APIC TIMER REGISTERS
===============================================================================

APIC_LVTT
----------
Timer Mode

APIC_TMICT
----------
Initial Count

APIC_TMCCT
----------
Current Count

APIC_TDCR
----------
Clock Divider


===============================================================================
SETTING UP APIC TIMER
===============================================================================

Function:

    setup_APIC_timer()

Flow:

setup_APIC_timer()
       |
       +--> Configure LVTT
       |
       +--> Configure Divider
       |
       +--> Load Initial Count
       |
       v
Timer Starts


Example:

LVTT

    Periodic Mode

TDCR

    Divide Clock By 16

TMICT

    Initial Count


Timer:

1000000
999999
999998
...
0

Interrupt Generated


===============================================================================
TIMER CALIBRATION
===============================================================================

Function:

    calibrate_APIC_clock()

Problem:

Need to know LAPIC timer frequency.


Method:

Start APIC Counter
        |
        v
Read TSC
        |
        v
Wait
        |
        v
Read APIC Counter
        |
        v
Read TSC
        |
        v
Compute Frequency


Formula:

APIC Delta
-----------
 TSC Delta

x CPU Frequency


Result:

Detected XXX MHz APIC timer


Saved:

    calibration_result


===============================================================================
TIMER INTERRUPT FLOW
===============================================================================

Hardware:

APIC Timer
      |
      v
Vector Interrupt
      |
      v
smp_apic_timer_interrupt()
      |
      +--> ack_APIC_irq()
      |
      +--> irq_enter()
      |
      +--> smp_local_timer_interrupt()
      |
      +--> irq_exit()


===============================================================================
WHAT HAPPENS ON EVERY TICK?
===============================================================================

Function:

    smp_local_timer_interrupt()

Work:

1. profile_tick()

2. update_process_times()

3. scheduler accounting

4. process runtime updates


Flow:

Timer Tick
      |
      +--> update user time
      |
      +--> update system time
      |
      +--> scheduler accounting
      |
      +--> profiling


This is the heartbeat of Linux scheduling.


===============================================================================
PART 5 : INTER PROCESSOR INTERRUPTS (IPI)
===============================================================================

BACKGROUND
==========

One CPU often needs another CPU to do work.

Example:

CPU0 modifies page table.

CPU1 still has old TLB entries.

Need:

    TLB Shootdown


CPU0
 |
 +--> Send IPI
 |
 v
CPU1
 |
 +--> Flush TLB


Flow:

CPU0 LAPIC
      |
      v
APIC Bus
      |
      v
CPU1 LAPIC


Uses:

    TLB Shootdown
    Reschedule
    CPU Stop
    CPU Startup
    Timer Broadcast


===============================================================================
TIMER BROADCAST MODE
===============================================================================

Some CPUs may disable local timer.

Then:

One CPU generates timer interrupt.

Then sends IPIs.

Flow:

CPU0 Timer
      |
      v
Broadcast IPI
      |
      +--> CPU1
      |
      +--> CPU2
      |
      +--> CPU3


Functions:

    switch_APIC_timer_to_ipi()

    smp_send_timer_broadcast_ipi()

    switch_ipi_to_APIC_timer()


===============================================================================
PART 6 : LAPIC POWER MANAGEMENT
===============================================================================

Connection To ACPI Suspend
==========================

When machine enters S3:

    CPU State Lost

LAPIC state also lost.


Need:

    Save LAPIC Registers
    Restore LAPIC Registers


===============================================================================
SUSPEND FLOW
===============================================================================

Function:

    lapic_suspend()

Saves:

    APIC_ID
    TASKPRI
    DFR
    LDR
    SPIV
    LVTT
    LVT0
    LVT1
    LVTERR
    TMICT
    TDCR


Stored in:

    apic_pm_state


Flow:

Suspend
   |
   v
Save LAPIC Registers
   |
   v
Power Off CPU


===============================================================================
RESUME FLOW
===============================================================================

Function:

    lapic_resume()

Called after:

    wakeup.S

Flow:

Resume
   |
   v
Restore APICBASE MSR
   |
   v
Restore APIC Registers
   |
   v
Restore Timer
   |
   v
Enable LAPIC


Connection:

acpi.c
   |
   v
wakeup.S
   |
   v
lapic_resume()
   |
   v
Normal Linux Execution


===============================================================================
SPURIOUS INTERRUPTS
===============================================================================

Handler:

    smp_spurious_interrupt()

Purpose:

Handle unexpected LAPIC interrupts.


Flow:

Unexpected Vector
        |
        v
Check ISR
        |
        +--> Real?
        |      |
        |      +--> ACK
        |
        +--> Spurious?
               |
               +--> Ignore


===============================================================================
APIC ERROR INTERRUPTS
===============================================================================

Handler:

    smp_error_interrupt()

Reads:

    APIC_ESR

Error Status Register


Possible Errors:

Bit 0

    Send Checksum Error

Bit 1

    Receive Checksum Error

Bit 2

    Send Accept Error

Bit 3

    Receive Accept Error

Bit 5

    Illegal Vector Sent

Bit 6

    Illegal Vector Received

Bit 7

    Illegal Register Access


Flow:

APIC Error
      |
      v
APIC_ESR
      |
      v
Print Debug Message
      |
      v
Continue


===============================================================================
COMPLETE INTERRUPT FLOW
===============================================================================

DEVICE IRQ

Network Card
      |
      v
IO APIC
      |
      v
Local APIC
      |
      v
CPU
      |
      v
Linux IRQ Handler


===============================================================================
COMPLETE TIMER FLOW
===============================================================================

APIC Timer
      |
      v
smp_apic_timer_interrupt()
      |
      v
ack_APIC_irq()
      |
      v
irq_enter()
      |
      v
smp_local_timer_interrupt()
      |
      +--> profile_tick()
      |
      +--> update_process_times()
      |
      +--> scheduler accounting
      |
      v
irq_exit()


===============================================================================
COMPLETE FILE FLOW
===============================================================================

Boot
 |
 +--> detect LAPIC
 |
 +--> map LAPIC MMIO
 |
 +--> map IOAPIC MMIO
 |
 +--> verify LAPIC
 |
 +--> setup LAPIC
 |
 +--> calibrate APIC timer
 |
 +--> start APIC timer
 |
 +--> system running
 |
 +--> Device IRQs
 |         |
 |         +--> IOAPIC -> LAPIC -> CPU
 |
 +--> Timer IRQs
 |         |
 |         +--> LAPIC Timer -> Scheduler
 |
 +--> IPIs
 |         |
 |         +--> CPU -> CPU communication
 |
 +--> Suspend
 |         |
 |         +--> lapic_suspend()
 |
 +--> Resume
           |
           +--> wakeup.S
           |
           +--> lapic_resume()
           |
           +--> continue execution

===============================================================================

KEY IDEA
========

This file turns the Local APIC into the central interrupt controller
for each CPU.

It provides:

    - Local interrupt delivery
    - APIC timer
    - CPU-to-CPU IPIs
    - Error handling
    - Suspend/Resume support

Without this file:

    No SMP IPIs
    No APIC timer
    No modern interrupt routing
    No LAPIC power management
    No scalable x86 interrupt subsystem
===============================================================================
