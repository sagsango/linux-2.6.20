memory managmnet related code!

This subsystem handles power management and hardware configuration functions provided by the system firmware (BIOS/UEFI).

This directory implements:
The ACPI interpreter that reads and executes AML (ACPI Machine Language) tables from firmware.
System-level ACPI functions — like sleep (S3), hibernate (S4), thermal management, battery and power adapters, buttons (power, lid), processor throttling, etc.
Integration with kernel subsystems like power management, CPU idle/clock control, and device enumeration.
