============================================================
AMD MCE THRESHOLD DRIVER
============================================================

FILE PURPOSE
------------------------------------------------------------

Provides:

    Correctable Error Thresholding

for AMD CPUs.

Instead of generating an MCE on every corrected error:

    ECC Error #1
    ECC Error #2
    ECC Error #3
    ...

hardware keeps a counter.

When threshold reached:

    APIC interrupt

is generated.

============================================================
HIGH LEVEL FLOW
============================================================

CPU Hardware
      |
      v
MCi_MISC threshold counter
      |
      v
Error Count++
      |
      v
Threshold reached?
      |
      +---- NO ---> continue
      |
      +---- YES
               |
               v
          APIC interrupt
               |
               v
     mce_threshold_interrupt()
               |
               v
           mce_log()

============================================================
AMD MACHINE CHECK ARCHITECTURE
============================================================

CPU
 |
 +-- Bank0
 |
 +-- Bank1
 |
 +-- Bank2
 |
 +-- Bank3
 |
 +-- Bank4
 |
 +-- Bank5

Each bank:

    MCA Error Tracking

Example:

Bank0

    Cache Errors

Bank4

    Memory Controller Errors

etc.

============================================================
KEY DATA STRUCTURES
============================================================

threshold_block

Represents one threshold counter.

struct threshold_block
{
    block
    bank
    cpu

    address
