bootsect.S
    |
    | Load setup + kernel from disk
    v
setup.S
    |
    | Collect BIOS information
    | Enable A20
    | Build GDT
    | Enter Protected Mode
    v
head.S (startup_32)
    |
    | Decompress kernel
    v
start_kernel()











===============================================================================
                    setup.S EXECUTION FLOW
===============================================================================


bootsect.S
    |
    | loaded setup.S at 0x90200
    |
    v

+----------------------+
| start_of_setup()     |
+----------------------+

        |
        | Verify loader signatures
        |
        v

+----------------------+
| Check SIG1/SIG2      |
| 0xAA55 / 0x5A5A      |
+----------------------+

        |
        +------ bad -----> relocate setup and retry
        |
        v
      good


===============================================================================
                CPU FEATURE VALIDATION
===============================================================================

        |
        v

+----------------------+
| CPUID available?     |
+----------------------+
        |
        +---- no ----> panic

        |
        v

+----------------------+
| Long Mode available? |
+----------------------+
        |
        +---- no ----> panic
        |
        v

+----------------------+
| SSE available?       |
+----------------------+
        |
        +---- no ----> try enable on AMD
        |
        v

Continue


===============================================================================
                    BIOS DATA COLLECTION
===============================================================================

        |
        v

+----------------------+
| E820 Memory Map      |
+----------------------+

BIOS INT 15h E820

        returns

+-------------------------------+
| RAM 0 - 640KB                 |
| Reserved                      |
| RAM 1MB - 4GB                 |
| ACPI                          |
| MMIO holes                    |
+-------------------------------+

stored at:

        0x90000 area


-------------------------------------------------------------------------------

        |
        v

+----------------------+
| Memory Size          |
+----------------------+

INT 15h E801
INT 15h 88h

fallback methods


-------------------------------------------------------------------------------

        |
        v

+----------------------+
| Video Detection      |
+----------------------+

video.S

Queries:

    VGA
    VESA
    available modes

Stores results in boot params


-------------------------------------------------------------------------------

        |
        v

+----------------------+
| Disk Geometry        |
+----------------------+

Reads BIOS disk tables

HD0
HD1

Stores:

    heads
    sectors
    cylinders


-------------------------------------------------------------------------------

        |
        v

+----------------------+
| Mouse Detection      |
+----------------------+

INT 11h

Stores presence flag


===============================================================================
                    PREPARE FOR PMODE
===============================================================================

        |
        v

+----------------------+
| default_switch()     |
+----------------------+

CLI

Disable interrupts

Disable NMI


===============================================================================
                KERNEL RELOCATION
===============================================================================


For zImage:

Kernel loaded at:

        0x10000

Need to move it lower.


        Source
        |
        v

+----------------------+
| 0x10000              |
| compressed kernel    |
+----------------------+

        |
        | do_move()
        v

+----------------------+
| final low memory     |
+----------------------+


For bzImage:

        LOADED_HIGH

Skip move completely.


===============================================================================
                BUILD DESCRIPTOR TABLES
===============================================================================

        |
        v

+----------------------+
| Build GDT            |
+----------------------+


Descriptor #0

    NULL


Descriptor #1

    Kernel Code

    Base  = 0
    Limit = 4GB


Descriptor #2

    Kernel Data

    Base  = 0
    Limit = 4GB


Memory:

+-------------------+
| NULL              |
+-------------------+
| CODE 4GB          |
+-------------------+
| DATA 4GB          |
+-------------------+

        |
        v

lgdt gdt_48


===============================================================================
                    ENABLE A20
===============================================================================


Without A20:

    0x000000
    0x100000

point to same location


+--------------------+
| A20 disabled       |
+--------------------+

        1MB wraps


Enable through:

    8042 keyboard controller

        and

    port 0x92


Wait until:

    0x200 != 0x100200


===============================================================================
                PREPARE TO LEAVE BIOS
===============================================================================

        |
        v

Mask PIC interrupts

OUT 0xA1 = 0xFF

OUT 0x21 = 0xFB


After this:

        BIOS is essentially done.


===============================================================================
                ENTER PROTECTED MODE
===============================================================================

        |
        v

movw $1,%ax
lmsw %ax


CR0.PE = 1


CPU state:

+---------------------+
| Protected Mode      |
+---------------------+


Flush pipeline:

        jmp flush_instr


===============================================================================
                BUILD POINTER FOR HEAD.S
===============================================================================

flush_instr:

        |
        v

Compute pointer to setup area


ESI =
    real-mode setup structure

This pointer is later passed to:

        startup_32


===============================================================================
                FAR JUMP TO 32-BIT CODE
===============================================================================


        66 EA

        FAR JUMP


For zImage:

        EIP = 0x1000


For bzImage:

        EIP = 0x100000


        |
        v

+----------------------+
| head.S               |
| startup_32           |
+----------------------+


===============================================================================
                    COMPLETE BOOT CHAIN
===============================================================================


BIOS
 |
 v
bootsect.S
 |
 | load kernel
 | load setup
 |
 v
setup.S
 |
 | CPUID check
 | Long mode check
 | Memory map
 | Video setup
 | Disk info
 | Mouse info
 |
 | Build GDT
 | Enable A20
 | Protected Mode
 |
 v
startup_32 (head.S)
 |
 | Setup stack
 | Clear BSS
 | Decompress kernel
 |
 v
start_kernel()
 |
 v
Linux Kernel
===============================================================================
