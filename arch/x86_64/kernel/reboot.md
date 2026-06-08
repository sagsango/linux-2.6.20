============================================================
x86-64 MACHINE REBOOT / HALT / POWER-OFF
File: arch/x86_64/kernel/reboot.c
Linux 2.6.x Era
============================================================

PURPOSE
------------------------------------------------------------

This file implements the architecture-specific final steps for:

    reboot
    emergency reboot
    halt
    power off

Generic kernel code decides:

    "We need to reboot."

This file performs:

    "How do we physically reset this x86 machine?"

============================================================
BIG PICTURE
============================================================

User runs:

    reboot

        |
        v

kernel_restart()

        |
        v

machine_restart()

        |
        +--> machine_shutdown()
        |
        +--> machine_emergency_restart()
        |
        v

hardware reset

============================================================
WHY REBOOT IS ARCH-SPECIFIC
------------------------------------------------------------

Different architectures reset differently.

On x86, old reset methods include:

    keyboard controller reset

    triple fault reset

    BIOS/firmware power-off hook

    chipset reset mechanisms

This file uses mainly:

    8042 keyboard controller reset

and

    triple fault reset

============================================================
IMPORTANT GLOBALS
============================================================

pm_power_off

    Function pointer used for platform-specific power-off.

    ACPI/APM/platform code may set this.

------------------------------------------------------------

reboot_type

    Which reset method to use.

    BOOT_KBD
        keyboard controller reset

    BOOT_TRIPLE
        triple fault reset

------------------------------------------------------------

reboot_mode

    Warm or cold reboot flag.

------------------------------------------------------------

reboot_force

    Skip shutdown cleanup and force reset.

============================================================
BOOT PARAMETER: reboot=
============================================================

Kernel command line:

    reboot=...

Options:

    reboot=w

        warm reboot

------------------------------------------------------------

    reboot=c

        cold reboot

------------------------------------------------------------

    reboot=t

        use triple fault

------------------------------------------------------------

    reboot=k

        use keyboard controller

------------------------------------------------------------

    reboot=f

        force reboot, skip shutdown path

Example:

    reboot=t,c

Means:

    cold reboot using triple fault

============================================================
WARM VS COLD REBOOT
============================================================

This file writes:

    0x472

BIOS Data Area reboot flag.

Code:

    *((unsigned short *)__va(0x472)) = reboot_mode;

============================================================

Warm reboot value:

    0x1234

Cold reboot value:

    0

============================================================

Old BIOS checks this location after reset.

Warm reboot:

    skip some memory tests

Cold reboot:

    full initialization

============================================================
machine_restart()
============================================================

Main restart function.

Flow:

    print "machine restart"

    if not reboot_force:
        machine_shutdown()

    machine_emergency_restart()

============================================================
machine_shutdown()
============================================================

Purpose:

    Stop other CPUs

    Shut down APIC interrupt routing

    Prepare machine for reset

============================================================
SMP FLOW
============================================================

On SMP:

    choose reboot CPU

    bind current task to reboot CPU

    send stop to all other CPUs

============================================================
WHY CHOOSE CPU0?
============================================================

The boot CPU is usually logical CPU 0.

Old firmware/platform reset paths often expect BSP-like behavior.

So reboot tries to run on CPU0.

============================================================
machine_shutdown() FLOW
============================================================

machine_shutdown()
      |
      +--> choose reboot CPU
      |
      +--> set_cpus_allowed(current, reboot_cpu)
      |
      +--> smp_send_stop()
      |
      +--> local_irq_save()
      |
      +--> disable local APIC if UP
      |
      +--> disable IOAPIC
      |
      +--> local_irq_restore()

============================================================
WHY STOP OTHER CPUs?
============================================================

During reboot, other CPUs must not keep running.

Otherwise:

    another CPU may handle interrupts

    another CPU may touch hardware

    another CPU may corrupt reboot state

    reset sequence may hang

So:

    smp_send_stop()

asks other CPUs to stop.

============================================================
WHY DISABLE IOAPIC?
============================================================

IOAPIC routes external interrupts to CPUs.

During reboot:

    interrupt routing should be quiet.

Otherwise:

    spurious interrupts may arrive

    firmware may see unexpected APIC state

    reset may hang on buggy hardware

============================================================
machine_emergency_restart()
============================================================

This is the real reset loop.

It never returns.

Flow:

    write warm/cold reboot flag

    forever:
        try selected reboot method

If one method fails, loop tries again.

============================================================
RESET METHOD 1: KEYBOARD CONTROLLER
============================================================

Old PC-compatible reset mechanism.

The 8042 keyboard controller has a CPU reset line.

Command:

    outb(0xfe, 0x64)

Meaning:

    pulse reset line low

============================================================
FLOW
============================================================

kb_wait()
      |
      v
wait until controller input buffer empty

      |
      v
outb(0xfe, 0x64)

      |
      v
hardware reset

============================================================
kb_wait()
============================================================

Reads port:

    0x64

Checks bit:

    0x02

Meaning:

    input buffer full

Loop until:

    input buffer empty

Then command can be written safely.

============================================================
KEYBOARD RESET FLOW
============================================================

CPU
 |
 v
I/O port 0x64
 |
 v
8042 keyboard controller
 |
 v
pulse reset line
 |
 v
CPU/system reset

============================================================
WHY KEYBOARD CONTROLLER?
============================================================

Historical PC design.

The keyboard controller controlled the A20 gate and reset line.

Even on newer systems, firmware/chipset often emulates this behavior.

============================================================
RESET METHOD 2: TRIPLE FAULT
============================================================

Triple fault is another classic x86 reset trick.

============================================================
BACKGROUND: FAULTS
============================================================

CPU takes exception.

Example:

    #BP from int3

CPU looks in:

    IDT

for exception handler.

============================================================
If exception handler itself faults:

    double fault

============================================================
If double fault handling fails:

    triple fault

============================================================
On x86:

    triple fault resets CPU

============================================================
HOW THIS FILE FORCES TRIPLE FAULT
============================================================

Code:

    lidt(&no_idt)

This loads an invalid/empty IDT.

Then:

    int3

This generates breakpoint exception.

CPU tries to handle #BP.

But IDT is invalid.

Fault while handling exception.

Eventually:

    triple fault

CPU resets.

============================================================
TRIPLE FAULT FLOW
============================================================

Load bad IDT
      |
      v
execute INT3
      |
      v
CPU tries #BP handler
      |
      v
IDT invalid
      |
      v
fault during fault handling
      |
      v
double fault
      |
      v
cannot handle
      |
      v
triple fault
      |
      v
CPU reset

============================================================
IMPORTANT DISTINCTION
============================================================

This use of INT3 is not debugging.

Here INT3 is used only as a convenient way to force an exception.

Because IDT is invalid, the exception cannot be handled.

That intentionally causes reset.

============================================================
WHY FALL BACK BETWEEN METHODS?
============================================================

Some machines:

    keyboard reset fails

Some machines:

    triple fault behaves differently

So the code loops and retries.

============================================================
machine_halt()
============================================================

Empty in this file.

Generic halt path may already do enough.

This architecture implementation does not add extra work here.

============================================================
machine_power_off()
============================================================

Power-off path.

Flow:

    if pm_power_off exists:
        if not reboot_force:
            machine_shutdown()

        pm_power_off()

============================================================
pm_power_off
============================================================

Function pointer.

Usually installed by:

    ACPI

    APM

    platform power management code

Example behavior:

    tell firmware/chipset to remove power

============================================================
POWER-OFF FLOW
============================================================

poweroff command
      |
      v
kernel_power_off()
      |
      v
machine_power_off()
      |
      +--> machine_shutdown()
      |
      +--> pm_power_off()
      |
      v
system powers off

============================================================
reboot_force
============================================================

If set:

    skip machine_shutdown()

Meaning:

    do not stop CPUs cleanly

    do not disable APICs

    just reset immediately

Useful when:

    shutdown path hangs

    system is badly broken

    emergency reboot needed

============================================================
COMPLETE NORMAL REBOOT FLOW
============================================================

reboot syscall
      |
      v
kernel_restart()
      |
      v
machine_restart()
      |
      v
machine_shutdown()
      |
      +--> move to reboot CPU
      +--> stop other CPUs
      +--> disable APICs
      |
      v
machine_emergency_restart()
      |
      +--> set warm/cold BIOS flag
      +--> keyboard reset or triple fault
      |
      v
hardware reset

============================================================
COMPLETE FORCED REBOOT FLOW
============================================================

reboot=f
      |
      v
machine_restart()
      |
      v
skip machine_shutdown()
      |
      v
machine_emergency_restart()
      |
      v
reset immediately

============================================================
COMPLETE TRIPLE FAULT REBOOT FLOW
============================================================

reboot=t
      |
      v
machine_emergency_restart()
      |
      v
load invalid IDT
      |
      v
execute INT3
      |
      v
triple fault
      |
      v
CPU reset

============================================================
COMPLETE KEYBOARD REBOOT FLOW
============================================================

reboot=k
      |
      v
machine_emergency_restart()
      |
      v
wait for 8042 ready
      |
      v
write 0xfe to port 0x64
      |
      v
pulse reset line
      |
      v
system reset

============================================================
RELATION TO FILES YOU STUDIED
============================================================

ptrace.c

    INT3 means breakpoint/debugging.

reboot.c

    INT3 is used to deliberately cause a fault
    after loading an invalid IDT.

kprobes.c

    INT3 is used to trap into kprobe handler.

machine_kexec.c

    Does not reset hardware.
    Jumps into another kernel.

reboot.c

    Actually resets or powers off machine.

============================================================
MENTAL MODEL
============================================================

machine_kexec.c:

    "Replace current kernel with another kernel."

reboot.c:

    "Physically reset or power off the machine."

ptrace/kprobe INT3:

    "Use INT3 as a breakpoint."

reboot INT3:

    "Use INT3 as an exception trigger to cause triple fault."

============================================================
ONE-LINE SUMMARY
============================================================

This reboot code prepares x86-64 for shutdown by stopping
other CPUs and disabling APIC interrupt routing, then resets
the machine either through the legacy keyboard controller reset
command or by intentionally loading an invalid IDT and executing
INT3 to force a triple fault, while power-off is delegated to
the platform-provided pm_power_off hook.
============================================================
