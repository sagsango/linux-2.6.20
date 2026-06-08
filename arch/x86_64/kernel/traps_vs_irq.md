```text
===============================================================================
FILE: TRAPS_vs_IRQs_x86_Linux_2.6.20.txt
PURPOSE: UNDERSTAND THE DIFFERENCE BETWEEN TRAPS AND INTERRUPTS (IRQs)
===============================================================================

BACKGROUND
===============================================================================

When the CPU is running instructions, something may happen that requires
immediate attention.

Examples:

    divide by zero
    page fault
    keyboard key pressed
    network packet arrived
    timer tick

All of these force the CPU to stop normal execution and enter the kernel.

On x86 these events are delivered through the IDT:

    IDT = Interrupt Descriptor Table

                +------------+
                |  IDT[0]    |
                +------------+
                |  IDT[1]    |
                +------------+
                |  IDT[2]    |
                +------------+
                |    ...     |
                +------------+
                | IDT[255]   |
                +------------+

Each entry points to a handler.

But not all events are the same.

There are two major categories:

    1. Traps / Exceptions
    2. Interrupt Requests (IRQs)

===============================================================================
BIG PICTURE
===============================================================================

                    CPU Running
                          |
                          v
                +------------------+
                |  Something Happens|
                +------------------+
                          |
            +-------------+-------------+
            |                           |
            v                           v

      CPU detected               External device
      internal event             generated event

            |                           |
            v                           v

      TRAP / EXCEPTION                IRQ

===============================================================================
PART 1: TRAPS (EXCEPTIONS)
===============================================================================

A trap originates INSIDE the CPU.

The CPU itself detects a condition.

Examples:

    divide by zero
    invalid opcode
    page fault
    general protection fault
    breakpoint

No hardware device is involved.

===============================================================================
EXAMPLE #1 DIVIDE BY ZERO
===============================================================================

User executes:

    mov $0, %ebx
    div %ebx

CPU detects:

    divisor == 0

CPU automatically generates:

    #DE (Divide Error)

Vector:

    0

Flow:

    User code
        |
        v
    div instruction
        |
        v
    CPU detects error
        |
        v
    IDT[0]
        |
        v
    do_divide_error()
        |
        v
    signal SIGFPE

===============================================================================
EXAMPLE #2 PAGE FAULT
===============================================================================

User:

    *0xdeadbeef = 1;

CPU tries translation:

    virtual -> physical

Fails.

CPU generates:

    #PF

Vector:

    14

Flow:

    memory access
          |
          v
     page walk
          |
          v
      not present
          |
          v
        #PF
          |
          v
    do_page_fault()
          |
          +--> allocate page
          |
          +--> SIGSEGV

===============================================================================
COMMON TRAPS
===============================================================================

Vector   Name

0        Divide Error (#DE)
1        Debug (#DB)
2        NMI
3        Breakpoint (#BP)
4        Overflow (#OF)
5        Bound Range
6        Invalid Opcode (#UD)
7        Device Not Available (#NM)
8        Double Fault (#DF)
13       General Protection (#GP)
14       Page Fault (#PF)

===============================================================================
PART 2: IRQs (INTERRUPTS)
===============================================================================

IRQs originate OUTSIDE the CPU.

Some hardware device wants service.

Examples:

    keyboard
    mouse
    disk
    NIC
    timer
    USB

===============================================================================
EXAMPLE: KEYBOARD
===============================================================================

User presses:

    A

Keyboard controller:

    key arrived

Raises IRQ.

Flow:

    Keyboard
         |
         v
      IRQ1
         |
         v
      IOAPIC
         |
         v
      LAPIC
         |
         v
      CPU
         |
         v
      IDT[vector]
         |
         v
      do_IRQ()
         |
         v
      keyboard driver

===============================================================================
EXAMPLE: NETWORK PACKET
===============================================================================

NIC receives packet.

NIC DMA writes packet.

NIC raises interrupt.

Flow:

    Ethernet Packet
           |
           v
        NIC
           |
           v
         IRQ
           |
           v
       do_IRQ()
           |
           v
      e1000_interrupt()
           |
           v
      NAPI poll
           |
           v
      TCP/IP stack

===============================================================================
TRAPS VS IRQS
===============================================================================

TRAP:

    Generated by CPU

IRQ:

    Generated by device

-------------------------------------------------------------------------------

TRAP:

    Internal event

IRQ:

    External event

-------------------------------------------------------------------------------

TRAP:

    Usually synchronous

IRQ:

    Usually asynchronous

-------------------------------------------------------------------------------

TRAP:

    Related to current instruction

IRQ:

    Independent of current instruction

-------------------------------------------------------------------------------

TRAP:

    divide by zero

IRQ:

    keyboard press

===============================================================================
SYNCHRONOUS VS ASYNCHRONOUS
===============================================================================

TRAP:

Always reproducible.

Example:

    mov $0,%rbx
    div %rbx

Every execution:

    #DE

Same instruction.

-------------------------------------------------------------------------------

IRQ:

Can occur at any time.

Example:

    network packet

Packet arrival timing is unpredictable.

===============================================================================
LINUX 2.6.20 FLOW
===============================================================================

TRAP PATH

    IDT Entry
         |
         v
    entry.S
         |
         v
    traps.c
         |
         v
    do_page_fault()
    do_general_protection()
    do_debug()
    etc

===============================================================================

IRQ PATH

    IDT Entry
         |
         v
    entry.S
         |
         v
    do_IRQ()
         |
         v
    irq_desc[]
         |
         v
    handler
         |
         v
    driver

===============================================================================
FILES TO STUDY IN 2.6.20
===============================================================================

TRAPS:

    arch/x86_64/kernel/traps.c

Contains:

    do_page_fault
    do_general_protection
    do_debug
    do_invalid_op

-------------------------------------------------------------------------------

IRQs:

    arch/x86_64/kernel/irq.c

Contains:

    do_IRQ

-------------------------------------------------------------------------------

ENTRY CODE:

    arch/x86_64/kernel/entry.S

Contains:

    divide_error
    page_fault
    debug
    common_interrupt

===============================================================================
ASCII IDT LAYOUT
===============================================================================

        IDT

    +----------------+
 0  | divide error   |
    +----------------+
 1  | debug          |
    +----------------+
 2  | NMI            |
    +----------------+
 3  | breakpoint     |
    +----------------+
 ...
13  | GP fault       |
    +----------------+
14  | page fault     |
    +----------------+
 ...
32  | timer IRQ      |
    +----------------+
33  | keyboard IRQ   |
    +----------------+
34  | cascade IRQ    |
    +----------------+
...
255 | APIC vectors   |
    +----------------+

Notice:

    0-31     = CPU exceptions/traps
    32+      = external interrupts

===============================================================================
WHERE DOES NMI FIT?
===============================================================================

NMI = Non Maskable Interrupt

Special case.

Generated externally:

    watchdog
    hardware failure
    performance monitoring

But handled like an exception vector:

    IDT[2]

Characteristics:

    Cannot be disabled by IF flag
    Higher priority
    Used for lockup detection

===============================================================================
MENTAL MODEL
===============================================================================

Think:

TRAP
====
CPU says:

    "The instruction you just executed caused a problem."

Examples:

    #PF
    #GP
    #DE
    #UD

-------------------------------------------------------------------------------

IRQ
===

Device says:

    "I need attention."

Examples:

    keyboard
    disk
    network
    timer

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

A trap/exception is generated synchronously by the CPU because of the
currently executing instruction (page fault, divide error, GP fault),
while an IRQ is generated asynchronously by external hardware (keyboard,
network card, timer) and enters Linux through do_IRQ() and the interrupt
subsystem.
===============================================================================
```

