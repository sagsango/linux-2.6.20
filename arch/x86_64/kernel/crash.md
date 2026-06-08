===============================================================================
KEXEC CRASH SHUTDOWN
File: arch/x86_64/kernel/crash.c
===============================================================================

PURPOSE
=======

This file contains x86-64 specific code used before jumping into the
crash kernel during kdump.

It runs after:

    panic()
        or
    fatal crash

and before:

    kexec crash kernel


Main goal:

    Put the machine into the safest possible state so that the crash
    kernel can boot and save the vmcore.


===============================================================================
BACKGROUND: KDUMP FLOW
===============================================================================

Normal system:

    First Kernel
        |
        v
    Running Linux


Crash happens:

    First Kernel
        |
        v
    panic / oops / fatal error
        |
        v
    machine_crash_shutdown()
        |
        v
    kexec into crash kernel
        |
        v
    Crash Kernel
        |
        v
    /proc/vmcore
        |
        v
    Save dump of old kernel memory


The crash kernel must run while the old crashed kernel memory is preserved.


===============================================================================
WHY SPECIAL SHUTDOWN IS NEEDED
===============================================================================

After a kernel panic, the system is unstable.

Problems:

    - Other CPUs may still be running
    - Interrupts may still arrive
    - LAPIC may still deliver interrupts
    - IO APIC may still route device interrupts
    - NMI watchdog may still fire
    - CPUs may corrupt old memory
    - Devices may keep doing DMA


So before booting the crash kernel, Linux tries to stop everything.

This file mainly handles:

    1. Stop other CPUs
    2. Disable local APIC
    3. Disable IO APIC
    4. Save CPU register state


===============================================================================
IMPORTANT GLOBAL
===============================================================================

static int crashing_cpu;

Meaning:

    CPU number of the CPU that is handling the crash.

Example:

    CPU2 panics

Then:

    crashing_cpu = 2


Why needed?

Because the crashing CPU may also receive an NMI.

The NMI crash handler must not halt the crashing CPU itself.


===============================================================================
SMP PROBLEM
===========

On multiprocessor systems:

    CPU0
    CPU1
    CPU2
    CPU3


If CPU2 crashes:

    CPU2 is the crashing CPU.

But:

    CPU0, CPU1, CPU3 may still be executing normal kernel code.

That is dangerous.

They may:

    - take locks
    - modify memory
    - handle interrupts
    - corrupt crash dump data


So the crashing CPU sends NMIs to all other CPUs.


===============================================================================
WHY NMI?
========

NMI = Non-Maskable Interrupt

Normal interrupt:

    can be disabled by local_irq_disable()

NMI:

    still gets delivered even if normal interrupts are disabled


In a crash, other CPUs may have interrupts disabled or be stuck.

So use NMI because it has a better chance of stopping them.


===============================================================================
SMP FLOW
========

Crashing CPU
      |
      v
nmi_shootdown_cpus()
      |
      +--> register NMI callback
      |
      +--> send NMI to all other CPUs
      |
      +--> wait up to 1 second
      |
      +--> disable local APIC


Other CPUs
      |
      v
receive NMI
      |
      v
crash_nmi_callback()
      |
      +--> save CPU registers
      |
      +--> disable local APIC
      |
      +--> decrement waiting counter
      |
      +--> halt forever


===============================================================================
WAIT COUNTER
============

static atomic_t waiting_for_crash_ipi;

Set to:

    num_online_cpus() - 1

Example:

    4 CPUs online

    waiting_for_crash_ipi = 3


Each non-crashing CPU does:

    atomic_dec(&waiting_for_crash_ipi);


Crashing CPU waits until:

    waiting_for_crash_ipi == 0

or timeout happens.


===============================================================================
NMI CALLBACK
============

Function:

    crash_nmi_callback()


Called when CPU receives NMI.

Flow:

crash_nmi_callback()
      |
      +--> Is this NMI_VECTOR crash IPI?
      |        |
      |        +--> No: ignore
      |
      +--> Get registers
      |
      +--> Get CPU number
      |
      +--> Is this crashing CPU?
      |        |
      |        +--> Yes: do not halt it
      |
      +--> Disable normal interrupts
      |
      +--> Save CPU state
      |
      +--> Disable local APIC
      |
      +--> Decrement wait counter
      |
      +--> halt forever


Why not halt crashing CPU?

Because crashing CPU must continue and jump into crash kernel.


===============================================================================
NMI CALLBACK DETAILS
===============================================================================

Code logic:

if (val != DIE_NMI_IPI)
    return NOTIFY_OK;

Meaning:

    Only handle NMI IPIs.


regs = ((struct die_args *)data)->regs;

Meaning:

    Get register snapshot at NMI time.


cpu = raw_smp_processor_id();

Meaning:

    Identify which CPU received NMI.


if (cpu == crashing_cpu)
    return NOTIFY_STOP;

Meaning:

    Do not stop the crashing CPU.


Then:

    local_irq_disable();
    crash_save_cpu(regs, cpu);
    disable_local_APIC();
    atomic_dec(&waiting_for_crash_ipi);

    for (;;)
        halt();


Result:

    Non-crashing CPU is frozen forever.


===============================================================================
SEND NMI TO OTHER CPUS
===============================================================================

Function:

    smp_send_nmi_allbutself()

Code:

    send_IPI_allbutself(NMI_VECTOR);


Meaning:

    Send NMI_VECTOR to every CPU except current CPU.


Diagram:

CPU2 crashes

CPU2
 |
 +--> send NMI
 |
 +--> CPU0
 +--> CPU1
 +--> CPU3


===============================================================================
NMI SHOOTDOWN
==============

Function:

    nmi_shootdown_cpus()

Flow:

nmi_shootdown_cpus()
      |
      +--> waiting = num_online_cpus() - 1
      |
      +--> register crash_nmi_callback
      |
      +--> memory barrier
      |
      +--> send NMI all but self
      |
      +--> wait up to 1000 ms
      |
      +--> disable local APIC


Why memory barrier?

    wmb();

Ensures NMI callback registration is visible before NMIs are sent.

Otherwise another CPU could receive the NMI before the handler is ready.


Timeout:

    Wait at most 1 second.

Why?

In a crash path, do not wait forever.

Some CPUs may already be dead or stuck.


===============================================================================
UNIPROCESSOR CASE
===============================================================================

If CONFIG_SMP is not enabled:

    nmi_shootdown_cpus()
    {
        // no CPUs to shoot down
    }


Only current CPU exists.


===============================================================================
MAIN FUNCTION
===============================================================================

Function:

    machine_crash_shutdown(struct pt_regs *regs)

This is the architecture-specific crash shutdown routine.


Flow:

machine_crash_shutdown()
      |
      +--> disable local interrupts
      |
      +--> record crashing CPU
      |
      +--> stop other CPUs using NMI
      |
      +--> disable current CPU LAPIC
      |
      +--> disable IO APIC
      |
      +--> save crashing CPU registers
      |
      v
ready for kexec crash kernel


===============================================================================
STEP 1 : DISABLE LOCAL INTERRUPTS
===============================================================================

Code:

    local_irq_disable();


Reason:

The kernel is already broken.

Do not allow more normal interrupts on the crashing CPU.


===============================================================================
STEP 2 : RECORD CRASHING CPU
===============================================================================

Code:

    crashing_cpu = smp_processor_id();


Example:

    panic happened on CPU1

Then:

    crashing_cpu = 1


Used by NMI callback to avoid halting the crash-control CPU.


===============================================================================
STEP 3 : SHOOT DOWN OTHER CPUS
===============================================================================

Code:

    nmi_shootdown_cpus();


SMP case:

    send NMI to all other CPUs

UP case:

    do nothing


Goal:

    Stop other CPUs from executing.


===============================================================================
STEP 4 : DISABLE LOCAL APIC
===============================================================================

Code:

    if (cpu_has_apic)
        disable_local_APIC();


Reason:

Stop LAPIC from delivering more local APIC interrupts.


===============================================================================
STEP 5 : DISABLE IO APIC
===============================================================================

Code:

    disable_IO_APIC();


Reason:

Stop external device interrupts from being routed to CPUs.


===============================================================================
STEP 6 : SAVE CRASHING CPU STATE
===============================================================================

Code:

    crash_save_cpu(regs, smp_processor_id());


Saves register state of the crashing CPU.

This information becomes part of crash dump notes.

Later debugging tools can show:

    register values at crash time


===============================================================================
COMPLETE SMP CRASH FLOW
===============================================================================

Example: CPU2 crashes in 4 CPU system.

CPU2
 |
 | panic()
 |
 v
machine_crash_shutdown()
 |
 +--> local_irq_disable()
 |
 +--> crashing_cpu = 2
 |
 +--> waiting_for_crash_ipi = 3
 |
 +--> register crash NMI notifier
 |
 +--> send NMI to CPU0, CPU1, CPU3
 |
 |       CPU0:
 |         crash_nmi_callback()
 |         save regs
 |         disable LAPIC
 |         halt
 |
 |       CPU1:
 |         crash_nmi_callback()
 |         save regs
 |         disable LAPIC
 |         halt
 |
 |       CPU3:
 |         crash_nmi_callback()
 |         save regs
 |         disable LAPIC
 |         halt
 |
 +--> wait until counter is 0 or timeout
 |
 +--> disable CPU2 LAPIC
 |
 +--> disable IO APIC
 |
 +--> save CPU2 regs
 |
 v
kexec crash kernel


===============================================================================
RELATION TO crash_dump.c
===============================================================================

This file:

    prepares machine for crash kernel

crash_dump.c:

    lets crash kernel read old memory


Flow:

First Kernel Crash
      |
      v
arch/x86_64/kernel/crash.c
      |
      +--> stop CPUs
      +--> disable APICs
      +--> save registers
      |
      v
kexec crash kernel
      |
      v
kernel/crash_dump.c
      |
      +--> copy_oldmem_page()
      |
      v
save /proc/vmcore


===============================================================================
RELATION TO APIC FILE
===============================================================================

This file uses APIC functions:

    disable_local_APIC()
    disable_IO_APIC()
    send_IPI_allbutself(NMI_VECTOR)


So APIC is used twice:

Normal running:

    deliver interrupts

Crash path:

    deliver NMI to stop CPUs
    then disable APIC


===============================================================================
KEY IDEA
========

This file is the emergency shutdown path before kdump.

It does not try to cleanly shut down Linux.

Instead, it does the minimum safe work:

    - freeze other CPUs
    - stop interrupt controllers
    - save CPU register states
    - let kexec jump into crash kernel


In crash handling, simplicity is more important than elegance.

The kernel is already broken, so this code avoids complex locking,
scheduling, memory allocation, or normal shutdown paths.
===============================================================================
