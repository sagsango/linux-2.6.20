# ============================================================

# POWER MANAGEMENT & ACPI (ONE-FILE NOTES)

# ============================================================

# ------------------------------------------------------------

# 1. OVERVIEW

# ------------------------------------------------------------

Power Management Goal:
Reduce power consumption while maintaining performance.

Levels:
CPU Level      → idle states (C-states), freq scaling (P-states)
Device Level   → power on/off (D-states)
System Level   → sleep/hibernate (S-states)
Platform Level → firmware interface (ACPI)

# ------------------------------------------------------------

# 2. WHAT IS ACPI?

# ------------------------------------------------------------

ACPI = Advanced Configuration and Power Interface

Key Idea:
Firmware (BIOS/UEFI)
↓ provides tables + AML code
OS (Linux)
↓ parses + executes
Controls hardware power + configuration

ACPI replaces:
legacy BIOS-based configuration

# ------------------------------------------------------------

# 3. ACPI ARCHITECTURE

# ------------------------------------------------------------

```
+----------------------+
| Firmware (BIOS/UEFI) |
+----------------------+
          ↓ ACPI Tables
+----------------------+
| Linux ACPI Subsystem |
+----------------------+
          ↓
+----------------------+
| Devices / CPU / RAM  |
+----------------------+
```

# ------------------------------------------------------------

# 4. IMPORTANT ACPI TABLES

# ------------------------------------------------------------

RSDP:
Root pointer → entry to ACPI

RSDT / XSDT:
List of all ACPI tables

DSDT:
Main table with AML code
Defines devices + power logic

SSDT:
Additional modular tables

FADT:
Global power info
sleep states, PM registers

MADT:
CPU cores, APIC info

# ------------------------------------------------------------

# 5. AML (ACPI MACHINE LANGUAGE)

# ------------------------------------------------------------

ACPI contains executable code (AML)

Examples:
_PTS → prepare to sleep
_WAK → wake
_STA → device status
_ON  → power on
_OFF → power off

OS runs AML via interpreter

# ------------------------------------------------------------

# 6. POWER STATES (VERY IMPORTANT)

# ------------------------------------------------------------

System (S-states):
S0 → ON
S1 → light sleep
S3 → suspend to RAM
S4 → hibernate (disk)
S5 → soft off

CPU (C-states):
C0 → running
C1 → halt
C2/C3 → deeper idle

Performance (P-states):
CPU freq/voltage scaling

Device (D-states):
D0 → ON
D1/D2 → intermediate
D3 → OFF

# ------------------------------------------------------------

# 7. SUSPEND FLOW (S3 EXAMPLE)

# ------------------------------------------------------------

User triggers suspend:
↓
Kernel calls _PTS
↓
Devices go low power
↓
CPU idle state
↓
System sleeps

Wakeup:
↓
Firmware resumes
↓
Kernel calls _WAK
↓
Devices restored

# ------------------------------------------------------------

# 8. LINUX ACPI FLOW

# ------------------------------------------------------------

Boot:
find RSDP
parse RSDT/XSDT
load DSDT/SSDT
build ACPI namespace

Runtime:
execute AML methods
control power states

Example:
acpi_evaluate_object("_ON")

# ------------------------------------------------------------

# 9. DEVICE ENUMERATION

# ------------------------------------------------------------

x86:
ACPI used for device discovery

ARM/RISC-V:
Device Tree used instead

# ------------------------------------------------------------

# 10. INTERRUPTS & EVENTS

# ------------------------------------------------------------

ACPI handles:
IRQ routing
power button
sleep/wake events
thermal interrupts

# ------------------------------------------------------------

# 11. THERMAL & BATTERY

# ------------------------------------------------------------

ACPI manages:
thermal zones
fan control
battery info
throttling

# ------------------------------------------------------------

# 12. COMMON PROBLEMS

# ------------------------------------------------------------

```
buggy firmware
broken AML
inconsistent device states
platform quirks
```

Linux often adds:
workarounds (quirks)

# ------------------------------------------------------------

# 13. ACPI vs DEVICE TREE

# ------------------------------------------------------------

ACPI:
dynamic
includes executable code (AML)
used in x86

Device Tree:
static description
data only
used in ARM

# ------------------------------------------------------------

# 14. KERNEL IMPLEMENTATION

# ------------------------------------------------------------

Linux ACPI code:
drivers/acpi/

Components:
ACPI core
AML interpreter
namespace
device binding

# ------------------------------------------------------------

# 15. KEY INTERVIEW SUMMARY

# ------------------------------------------------------------

ACPI provides firmware-defined tables and executable AML code
to the OS, enabling control over system, CPU, and device power
states such as S-states, C-states, and P-states.

# ============================================================

# END

# ============================================================

