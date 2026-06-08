CPU Temperature Too High
          |
          v
Intel Thermal Hardware
          |
          v
Local APIC Thermal Interrupt
          |
          v
smp_thermal_interrupt()
          |
          v
Log Thermal Throttling Event






============================================================
INTEL THERMAL MONITORING
============================================================

FILE PURPOSE
------------------------------------------------------------

Enable and handle Intel CPU thermal throttling interrupts.

NOT:

    Machine Check Errors
    ECC Errors
    Cache Errors

YES:

    CPU overheating
    Thermal throttling
    TM1/TM2 events

============================================================
HIGH LEVEL FLOW
============================================================

CPU gets hot
      |
      v
Temperature reaches threshold
      |
      v
Intel Thermal Logic
      |
      v
APIC Thermal Interrupt
      |
      v
smp_thermal_interrupt()
      |
      v
therm_throt_process()
      |
      v
mce_log_therm_throt_event()

============================================================
TM1 vs TM2
============================================================

TM = Thermal Monitor

TM1
----
Old mechanism.

CPU automatically inserts idle cycles.

Example:

CPU running at 3 GHz

Thermal emergency

CPU runs only 50% duty cycle

Effective:

1.5 GHz

============================================================

TM2
----
Newer mechanism.

CPU lowers:

    multiplier
    voltage

Example:

3 GHz

becomes

1.6 GHz

More efficient.

============================================================
INITIALIZATION FLOW
============================================================

mce_intel_feature_init()
        |
        v
intel_init_thermal()

============================================================
intel_init_thermal()
============================================================

Checks:

    ACPI support

        X86_FEATURE_ACPI

    Thermal monitoring support

        X86_FEATURE_ACC

If either missing:

    return

============================================================
BIOS OWNERSHIP CHECK
============================================================

Reads:

MSR_IA32_MISC_ENABLE

and

APIC_LVTTHMR

If:

    TM1 enabled

and

    Thermal interrupt delivered as SMI

Then:

    BIOS owns thermal handling

Kernel exits.

============================================================

Reason:

BIOS SMM
    |
    v
already handling thermal events

Kernel must not interfere.

============================================================
SMI PATH
============================================================

CPU Hot
    |
    v
SMI
    |
    v
System Management Mode
    |
    v
BIOS Thermal Code

Kernel never sees event.

============================================================
APIC THERMAL VECTOR SETUP
============================================================

Reads:

APIC_LVTTHMR

If vector already installed:

    return

Otherwise:

    THERMAL_APIC_VECTOR

is installed.

============================================================
LOCAL APIC
============================================================

Local APIC

     +----------------+
     | Timer          |
     | Error          |
     | PMI            |
     | Thermal        |
     +----------------+

Thermal uses:

    APIC_LVTTHMR

============================================================
THERMAL INTERRUPT ENABLE
============================================================

MSR:

MSR_IA32_THERM_INTERRUPT

Kernel enables:

    threshold interrupts

Code:

    wrmsr(..., l | 0x03, ...)

============================================================
THERMAL MONITOR ENABLE
============================================================

MSR:

MSR_IA32_MISC_ENABLE

Bit:

    1 << 3

Enables:

    Thermal Monitoring

Code:

    wrmsr(..., l | (1<<3), ...)

============================================================
UNMASK APIC ENTRY
============================================================

Initially:

    APIC_LVT_MASKED

Then:

    APIC_LVT_MASKED cleared

Meaning:

    Interrupt now active

============================================================
THERMAL INTERRUPT HANDLER
============================================================

smp_thermal_interrupt()

============================================================

Flow:

CPU Hot
      |
      v
APIC interrupt
      |
      v
smp_thermal_interrupt()

============================================================

Handler:

ack_APIC_irq()

        acknowledge interrupt

exit_idle()

        leave idle state

irq_enter()

        generic IRQ entry

============================================================

Read thermal status:

MSR_IA32_THERM_STATUS

============================================================

Bit0:

    thermal status

1

    throttling active

0

    normal

============================================================

Code:

therm_throt_process(msr_val & 1)

============================================================

If thermal throttle occurred:

mce_log_therm_throt_event()

records event.

============================================================
THERMAL LOGGING
============================================================

Log contains:

CPU number

Thermal status MSR

Timestamp

Throttle state

============================================================
FULL RUNTIME FLOW
============================================================

CPU temperature rises
         |
         v
Thermal threshold crossed
         |
         v
TM1/TM2 activates
         |
         v
APIC Thermal Interrupt
         |
         v
smp_thermal_interrupt()
         |
         +--> read THERM_STATUS MSR
         |
         +--> therm_throt_process()
         |
         +--> mce_log_therm_throt_event()
         |
         v
Return

============================================================
IMPORTANT REGISTERS
============================================================

MSR_IA32_MISC_ENABLE

    Enable thermal monitoring

------------------------------------------------------------

MSR_IA32_THERM_STATUS

    Current thermal state

------------------------------------------------------------

MSR_IA32_THERM_INTERRUPT

    Interrupt control

------------------------------------------------------------

APIC_LVTTHMR

    Local APIC Thermal Vector

============================================================
RELATION TO MCE
============================================================

AMD threshold file:

    Correctable hardware errors

        ECC
        cache
        memory controller

------------------------------------------------------------

This Intel file:

    Thermal events only

        overheating
        throttling
        temperature alarms

============================================================
ONE-LINE SUMMARY
============================================================

This file enables Intel TM1/TM2 thermal monitoring,
programs the Local APIC thermal interrupt, and logs CPU
thermal throttling events whenever the processor becomes
hot enough to trigger hardware thermal protection.
============================================================




mce_amd.c
    |
    +--> ECC/cache/memory errors
    +--> threshold counters
    +--> APIC interrupt

intel_thermal.c
    |
    +--> overheating
    +--> TM1/TM2 throttling
    +--> APIC thermal interrupt

machine_kexec.c
    |
    +--> reboot into another kernel
