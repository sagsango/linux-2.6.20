===============================================================================
x86_64 IRQ ENTRY + IRQ STATISTICS
File: arch/x86_64/kernel/irq.c
===============================================================================

PURPOSE
=======

This file handles the lowest x86_64-specific IRQ dispatch layer.

It does not implement all interrupt logic.

Most IRQ logic is in:

    kernel/irq/
    i8259.c
    io_apic.c
    apic.c


This file mainly does:

    1. show interrupt statistics
    2. convert CPU vector -> Linux IRQ number
    3. call generic_handle_irq()
    4. run softirqs on interrupt stack
    5. fix IRQ affinity during CPU hotplug


===============================================================================
BIG PICTURE
===============================================================================

Hardware interrupt
      |
      v
CPU vector
      |
      v
entry.S IRQ stub
      |
      v
do_IRQ()
      |
      +--> vector -> IRQ number
      |
      +--> generic_handle_irq()
      |
      v
driver interrupt handler


===============================================================================
IMPORTANT GLOBAL
===============================================================================

atomic_t irq_err_count;

Counts interrupt/APIC/PIC errors.

Shown in:

    /proc/interrupts

as:

    ERR:


===============================================================================
STACK OVERFLOW DEBUG CHECK
===============================================================================

Enabled with:

    CONFIG_DEBUG_STACKOVERFLOW


Function:

    stack_overflow_check()


Purpose:

    Detect if kernel stack is close to overflowing.


Logic:

    current stack pointer near thread_info area?
        |
        v
    print warning and stack trace


Why only probabilistic?

Reliable stack checking on every interrupt would be expensive.


===============================================================================
show_interrupts()
===============================================================================

Purpose:

    Print /proc/interrupts contents.


Example output:

           CPU0       CPU1
  0:        123        456  IO-APIC-edge  timer
  1:          2          0  IO-APIC-edge  keyboard
NMI:          5          5
LOC:       9999       9999
ERR:          0


Flow:

show_interrupts()
      |
      +--> print CPU headers
      |
      +--> for each IRQ:
      |       |
      |       +--> lock irq_desc
      |       +--> read irq actions
      |       +--> print per-CPU IRQ count
      |       +--> print chip name
      |       +--> print handler/action names
      |
      +--> print NMI counts
      |
      +--> print local APIC timer counts
      |
      +--> print error count


===============================================================================
IRQ DESCRIPTOR
===============================================================================

Each Linux IRQ has:

    irq_desc[irq]


Contains:

    chip
    handler
    action
    lock
    status
    affinity


show_interrupts() reads:

    irq_desc[i].chip->name
    irq_desc[i].name
    irq_desc[i].action->name


===============================================================================
do_IRQ()
===============================================================================

This is the core normal-device interrupt entry.

Prototype:

    asmlinkage unsigned int do_IRQ(struct pt_regs *regs)


Called from:

    entry.S IRQ stubs


===============================================================================
do_IRQ() FLOW
===============================================================================

do_IRQ(regs)
      |
      +--> save old irq_regs
      |
      +--> extract vector from regs->orig_rax
      |
      +--> exit_idle()
      |
      +--> irq_enter()
      |
      +--> vector -> IRQ lookup
      |
      +--> optional stack overflow check
      |
      +--> generic_handle_irq(irq)
      |
      +--> irq_exit()
      |
      +--> restore old irq_regs
      |
      v
return


===============================================================================
WHY regs->orig_rax HOLDS VECTOR
===============================================================================

In entry.S, interrupt stubs push the vector encoded in orig_rax.

Code here does:

    vector = ~regs->orig_rax;


Meaning:

    entry.S stored the bitwise inverse of vector.

This recovers the real CPU interrupt vector.


===============================================================================
VECTOR TO IRQ TRANSLATION
===============================================================================

CPU receives:

    vector

Linux needs:

    irq number


Lookup:

    irq = __get_cpu_var(vector_irq)[vector];


Example:

    vector 0x31
        |
        v
    vector_irq[0x31] = IRQ16


Then:

    generic_handle_irq(16)


===============================================================================
RUNTIME INTERRUPT FLOW
===============================================================================

Example: network card interrupt

NIC
 |
 v
IO-APIC
 |
 v
Local APIC
 |
 v
CPU vector 0x51
 |
 v
entry.S IRQ stub
 |
 v
do_IRQ()
 |
 +--> vector_irq[0x51] = IRQ16
 |
 v
generic_handle_irq(16)
 |
 v
NIC driver ISR


===============================================================================
irq_enter() / irq_exit()
===============================================================================

irq_enter()
-----------

Marks CPU as being in hard interrupt context.

Effects:

    preempt_count updated
    interrupt accounting starts


irq_exit()
----------

Leaves hard interrupt context.

May run softirqs if pending.


Flow:

do_IRQ()
    |
    +--> irq_enter()
    |
    +--> handle interrupt
    |
    +--> irq_exit()


===============================================================================
generic_handle_irq()
===============================================================================

This is generic kernel IRQ code.

It calls the registered handler for that IRQ.

Flow:

generic_handle_irq(irq)
      |
      v
irq_desc[irq].handle_irq()
      |
      v
chip-specific ack/mask/eoi
      |
      v
driver action handler


Example:

    keyboard_interrupt()
    e1000_intr()
    ahci_interrupt()


===============================================================================
NO IRQ HANDLER CASE
===============================================================================

If vector has no valid IRQ:

    printk:
        No irq handler for vector


Usually means:

    bad routing
    stale vector
    APIC/IO-APIC bug
    interrupt after teardown


===============================================================================
CPU HOTPLUG: fixup_irqs()
===============================================================================

Enabled with:

    CONFIG_HOTPLUG_CPU


Purpose:

    When a CPU goes offline, move IRQs away from it.


Flow:

fixup_irqs(online_cpu_map)
      |
      +--> for each IRQ
      |
      +--> compute new affinity
      |
      +--> if affinity has no online CPU:
      |       |
      |       +--> break affinity and use online map
      |
      +--> call chip->set_affinity()
      |
      +--> wait 1 ms for pending IRQs


Why?

An IRQ must not target an offline CPU.


===============================================================================
SOFTIRQ HANDLING
===============================================================================

Function:

    do_softirq()


Purpose:

    Run pending softirqs outside hard IRQ context.


Softirq examples:

    TIMER_SOFTIRQ
    NET_RX_SOFTIRQ
    NET_TX_SOFTIRQ
    TASKLET_SOFTIRQ


===============================================================================
do_softirq() FLOW
===============================================================================

do_softirq()
      |
      +--> if already in interrupt:
      |       |
      |       +--> return
      |
      +--> disable local IRQs
      |
      +--> read pending softirq mask
      |
      +--> if pending:
      |       |
      |       +--> call_softirq()
      |
      +--> restore local IRQs


===============================================================================
WHY call_softirq()
===============================================================================

call_softirq() is assembly helper from entry.S.

It switches to interrupt stack before calling:

    __do_softirq()


Reason:

    softirq work can be deep/heavy

Using interrupt stack protects task stack from overflow.


Flow:

do_softirq()
      |
      v
call_softirq()
      |
      v
switch to IRQ stack
      |
      v
__do_softirq()


===============================================================================
WHY do_softirq() RETURNS IF in_interrupt()
===============================================================================

Code:

    if (in_interrupt())
        return;


Reason:

    Do not recursively run softirqs from interrupt context.

Softirqs should run when safe.


===============================================================================
RELATION TO entry.S
===============================================================================

entry.S does low-level CPU entry:

    save registers
    build pt_regs
    call do_IRQ()


irq.c does:

    vector -> irq
    call generic IRQ layer


Flow:

entry.S
    |
    v
do_IRQ()
    |
    v
generic_handle_irq()


===============================================================================
RELATION TO io_apic.c
===============================================================================

io_apic.c assigns:

    IRQ -> vector

and fills:

    per_cpu(vector_irq)[vector] = irq


irq.c uses that mapping:

    vector -> IRQ


So:

    io_apic.c programs routing
    irq.c dispatches runtime interrupt


===============================================================================
RELATION TO i8259.c
===============================================================================

i8259.c installs IDT gates and legacy PIC IRQs.

irq.c receives those vectors in do_IRQ().


===============================================================================
KEY IDEA
===============================================================================

irq.c is the thin architecture-specific bridge between CPU vectors and
the generic Linux IRQ subsystem.

It does not know device details.

It only knows:

    CPU gave me vector X
        |
        v
    vector_irq[X] says Linux IRQ Y
        |
        v
    generic_handle_irq(Y)


Everything after that is handled by the generic IRQ layer and the specific
IRQ chip code.
===============================================================================
