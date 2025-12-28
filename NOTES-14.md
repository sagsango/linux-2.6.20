# PIC/IOAPIC/LAPIC
================================================================================
              PIC / IO-APIC / LAPIC — FROM BASICS TO MODERN SYSTEMS
                     ONE GIANT CONTINUOUS ASCII FILE
================================================================================

Goal:
  Explain INTERRUPT CONTROLLERS from absolute basics to modern x86 systems:
    - PIC (8259A)
    - IO-APIC
    - Local APIC (LAPIC)
    - How they evolved
    - How interrupts flow
    - How Linux uses them
    - How virtualization fits (briefly, structurally)

No shortcuts. No hand-waving. One mental model.

================================================================================
SECTION 0: WHAT AN INTERRUPT REALLY IS (START FROM ZERO)
================================================================================

Definition:
  An interrupt is a hardware signal that forces the CPU to:
    1) Stop current execution
    2) Save state
    3) Jump to a predefined handler (via IDT)
    4) Resume later

Very important distinction:

  EXCEPTION (fault/trap):
    - Caused by the current instruction
    - Synchronous
    - CPU-internal
    - Examples: page fault, divide error

  INTERRUPT:
    - Caused by external hardware
    - Asynchronous
    - Device-driven
    - Examples: keyboard, disk, network

This document is ONLY about INTERRUPTS.

================================================================================
SECTION 1: THE ORIGINAL MODEL — PIC (8259A)
================================================================================

Time period:
  - Early x86 (8086 → Pentium)
  - Single CPU assumption
  - Very simple hardware

--------------------------------------------------------------------------------
1.1 WHAT IS THE PIC?
--------------------------------------------------------------------------------

PIC = Programmable Interrupt Controller

- External chip (Intel 8259A)
- Collects interrupt lines from devices
- Decides which interrupt goes to CPU
- Provides a VECTOR number

Single CPU model.

--------------------------------------------------------------------------------
1.2 BASIC PIC HARDWARE VIEW
--------------------------------------------------------------------------------

                +-------------------+
                |       CPU         |
                |-------------------|
                |  INTR pin         |
                +---------^---------+
                          |
                          |
                 +--------+--------+
                 |       PIC       |
                 |  (8259A)        |
                 |-----------------|
                 | IRQ0  (timer)   |
                 | IRQ1  (keyboard)|
                 | IRQ2             |
                 | IRQ3             |
                 | IRQ4             |
                 | IRQ5             |
                 | IRQ6             |
                 | IRQ7             |
                 +-----------------+

- PIC has 8 interrupt input lines: IRQ0..IRQ7
- CPU has ONE interrupt pin (INTR)

--------------------------------------------------------------------------------
1.3 CASCADING (MASTER + SLAVE PIC)
--------------------------------------------------------------------------------

To get 16 IRQs, two PICs are cascaded:

                +-------------------+
                |       CPU         |
                |-------------------|
                |  INTR             |
                +---------^---------+
                          |
                 +--------+--------+
                 |   MASTER PIC    |
                 |-----------------|
                 | IRQ0  Timer     |
                 | IRQ1  Keyboard  |
                 | IRQ2 ----------+------+
                 | IRQ3           |      |
                 | IRQ4           |      |
                 | IRQ5           |      |
                 | IRQ6           |      |
                 | IRQ7           |      |
                 +-----------------+      |
                                          |
                                  +-------+--------+
                                  |    SLAVE PIC    |
                                  |-----------------|
                                  | IRQ8            |
                                  | IRQ9            |
                                  | IRQ10           |
                                  | IRQ11           |
                                  | IRQ12           |
                                  | IRQ13           |
                                  | IRQ14 (disk)    |
                                  | IRQ15           |
                                  +-----------------+

IRQ2 on master is the cascade line.

--------------------------------------------------------------------------------
1.4 PIC INTERRUPT FLOW
--------------------------------------------------------------------------------

Example: Keyboard interrupt (IRQ1)

  Keyboard asserts IRQ1
        |
        v
  PIC sees IRQ1
        |
        v
  PIC raises INTR to CPU
        |
        v
  CPU acknowledges interrupt
        |
        v
  PIC supplies interrupt VECTOR (e.g., 0x21)
        |
        v
  CPU indexes IDT[0x21]
        |
        v
  Interrupt handler runs
        |
        v
  Handler sends EOI to PIC
        |
        v
  PIC re-enables IRQ line

--------------------------------------------------------------------------------
1.5 LIMITATIONS OF PIC
--------------------------------------------------------------------------------

- Only ONE CPU
- All interrupts go to same CPU
- Fixed priority scheme
- Shared interrupt lines
- Slow
- Hard to scale
- No NUMA awareness

Conclusion:
  PIC does not scale beyond very early systems.

================================================================================
SECTION 2: THE PROBLEM — MULTI-CPU SYSTEMS
================================================================================

As CPUs evolved:

- SMP (multiple CPUs)
- NUMA
- Many devices
- High interrupt rates

Key problem:
  "Which CPU should handle which interrupt?"

PIC cannot answer this.

Solution:
  Split interrupt control into TWO PARTS:
    - One part near devices
    - One part per CPU

This leads to:
  IO-APIC  +  LAPIC

================================================================================
SECTION 3: IO-APIC — DEVICE-SIDE INTERRUPT CONTROLLER
================================================================================

--------------------------------------------------------------------------------
3.1 WHAT IS IO-APIC?
--------------------------------------------------------------------------------

IO-APIC = I/O Advanced Programmable Interrupt Controller

- Replaces PIC for external interrupts
- Handles interrupts from devices
- Can route interrupts to ANY CPU
- Supports many more IRQ lines (24, 48, 64+)

--------------------------------------------------------------------------------
3.2 IO-APIC HARDWARE VIEW
--------------------------------------------------------------------------------

                   +----------------------+
                   |      CPU 0           |
                   |   Local APIC (LAPIC) |
                   +----------^-----------+
                              |
                   +----------+-----------+
                   |      CPU 1           |
                   |   Local APIC (LAPIC) |
                   +----------^-----------+
                              |
                              |
               +--------------+--------------+
               |            IO-APIC           |
               |------------------------------|
               | INT0  (network)              |
               | INT1  (disk)                 |
               | INT2  (USB)                  |
               | INT3  (...)                  |
               | ...                          |
               +--------------+--------------+
                              |
               +--------------+--------------+
               |        PCI / ISA DEVICES     |
               +------------------------------+

IO-APIC is usually memory-mapped.

--------------------------------------------------------------------------------
3.3 IO-APIC REDIRECTION TABLE
--------------------------------------------------------------------------------

Each input pin has an entry:

  IO-APIC Redirection Entry:
    - Vector number
    - Destination CPU (or CPU mask)
    - Trigger mode (edge / level)
    - Polarity
    - Mask bit

Simplified view:

  INTn --> [ vector | dest_cpu | trigger | masked ]

--------------------------------------------------------------------------------
3.4 IO-APIC INTERRUPT FLOW
--------------------------------------------------------------------------------

Example: Network card interrupt

  Network card asserts INT5
        |
        v
  IO-APIC sees INT5
        |
        v
  IO-APIC looks up redirection entry
        |
        v
  Determines:
     - vector = 0x45
     - destination = CPU 1
        |
        v
  Sends interrupt message to CPU 1 LAPIC
        |
        v
  LAPIC delivers interrupt to CPU core

IMPORTANT:
  IO-APIC does NOT talk to CPU core directly.
  It talks to LAPIC.

================================================================================
SECTION 4: LAPIC — PER-CPU INTERRUPT CONTROLLER
================================================================================

--------------------------------------------------------------------------------
4.1 WHAT IS LAPIC?
--------------------------------------------------------------------------------

LAPIC = Local Advanced Programmable Interrupt Controller

- One LAPIC per CPU core
- Integrated into the CPU
- Handles:
    - External interrupts (from IO-APIC)
    - Inter-Processor Interrupts (IPIs)
    - Timers
    - Performance monitoring interrupts

--------------------------------------------------------------------------------
4.2 LAPIC INTERNAL VIEW
--------------------------------------------------------------------------------

                +--------------------------------+
                |            CPU CORE            |
                |--------------------------------|
                |   Registers, execution units   |
                |--------------------------------|
                |         LOCAL APIC             |
                |--------------------------------|
                |  IRR  (Interrupt Request Reg) |
                |  ISR  (In-Service Reg)        |
                |  TMR  (Trigger Mode Reg)      |
                |  LVT entries                  |
                |  APIC ID                      |
                +--------------------------------+

--------------------------------------------------------------------------------
4.3 LAPIC INTERRUPT FLOW
--------------------------------------------------------------------------------

  Interrupt arrives at LAPIC
        |
        v
  LAPIC sets bit in IRR
        |
        v
  CPU checks interrupt priority
        |
        v
  CPU acknowledges interrupt
        |
        v
  LAPIC moves vector to ISR
        |
        v
  CPU jumps via IDT[vector]
        |
        v
  Handler runs
        |
        v
  EOI written to LAPIC
        |
        v
  LAPIC clears ISR bit

--------------------------------------------------------------------------------
4.4 LAPIC TIMER & IPIs
--------------------------------------------------------------------------------

LAPIC also generates interrupts itself:

- LAPIC timer → scheduler tick
- IPIs:
    - TLB shootdowns
    - CPU startup
    - Rescheduling

Example IPI flow:

  CPU0 wants CPU1 attention
        |
        v
  CPU0 writes to LAPIC ICR
        |
        v
  LAPIC hardware sends IPI
        |
        v
  CPU1 LAPIC receives interrupt
        |
        v
  CPU1 executes handler

================================================================================
SECTION 5: PUTTING IT ALL TOGETHER (MODERN SYSTEM)
================================================================================

Complete interrupt topology:

                +------------------------------------------------+
                |                    CPU 0                       |
                |        +-----------------------------+         |
                |        |        LOCAL APIC            |         |
                |        +--------------^--------------+         |
                +-----------------------|------------------------+
                                        |
                +------------------------------------------------+
                |                    CPU 1                       |
                |        +-----------------------------+         |
                |        |        LOCAL APIC            |         |
                |        +--------------^--------------+         |
                +-----------------------|------------------------+
                                        |
                     APIC message bus   |
                                        |
                +-----------------------+------------------------+
                |                     IO-APIC                   |
                |------------------------------------------------|
                | INT0  | INT1 | INT2 | INT3 | ... | INTn        |
                +-----------------------^------------------------+
                                        |
                +-----------------------+------------------------+
                |                DEVICES (PCI/ISA)               |
                +------------------------------------------------+

PIC is usually DISABLED in modern systems.

================================================================================
SECTION 6: PIC vs IO-APIC vs LAPIC (SIDE-BY-SIDE)
================================================================================

+----------------+-------------+----------------+----------------+
| Feature        | PIC         | IO-APIC        | LAPIC          |
+----------------+-------------+----------------+----------------+
| Location       | External IC | Chipset        | Inside CPU     |
| CPUs supported | 1           | Many           | Per-CPU        |
| IRQ lines      | 8 / 16      | 24+            | N/A            |
| Routing        | Fixed       | Programmable   | Final delivery |
| IPIs           | No          | No             | Yes            |
| Timer          | No          | No             | Yes            |
| Modern usage   | Legacy      | Yes            | Yes            |
+----------------+-------------+----------------+----------------+

================================================================================
SECTION 7: LINUX VIEW (MENTAL MODEL)
================================================================================

Linux logical flow:

  Device raises interrupt
        |
        v
  IO-APIC (or PIC) hardware
        |
        v
  LAPIC of target CPU
        |
        v
  CPU vector -> IDT
        |
        v
  common_interrupt()
        |
        v
  irq_desc[irq].handle_irq()
        |
        v
  Device driver ISR

PIC:
  - legacy
  - disabled on SMP systems

IO-APIC + LAPIC:
  - normal path

================================================================================
SECTION 8: WHY THIS MATTERS FOR VIRTUALIZATION (BRIDGE)
================================================================================

In virtual machines:

- PIC, IO-APIC, LAPIC are ALL EMULATED or VIRTUALIZED
- KVM injects interrupts into virtual LAPICs
- QEMU models IO-APIC / PIC behavior
- Real hardware interrupts are translated into virtual ones

Mental bridge:

  Real device interrupt
        |
        v
  Host IO-APIC + LAPIC
        |
        v
  Host kernel
        |
        v
  QEMU / KVM
        |
        v
  Virtual LAPIC
        |
        v
  Guest CPU

================================================================================
FINAL TAKEAWAY (PIN THIS)
================================================================================

PIC:
  - Old
  - Single CPU
  - Simple

IO-APIC:
  - Device-side routing
  - Scalable
  - Programmable

LAPIC:
  - Per-CPU
  - Final interrupt delivery
  - Handles IPIs and timers

Modern x86 interrupt delivery ALWAYS ends at:
  LAPIC -> CPU core -> IDT -> handler

================================================================================
END OF FILE
================================================================================


