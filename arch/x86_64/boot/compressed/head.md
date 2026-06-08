===============================================================================
                    Linux Compressed Kernel Boot Flow
                         (bootsect -> setup -> head.S)
===============================================================================


PHASE 1 : BIOS
--------------

Power ON
   |
   v
+--------------------+
| BIOS               |
+--------------------+
   |
   | Read first sector from boot device
   |
   v

Physical Memory
+---------------------------------------------------------+
| 0x00007C00                                              |
|                                                         |
| boot sector (bootsect.S)                               |
+---------------------------------------------------------+


CPU starts executing at:

    0000:7C00

or

    07C0:0000

(same physical address)



===============================================================================
PHASE 2 : BOOTSECT
===============================================================================

bootsect.S

        jmpl $BOOTSEG,$start2

                |
                v

Normalize CS

Before:

    CS:IP = 0000:7C00

After:

    CS:IP = 07C0:start2


Then:

    DS = CS
    ES = CS
    SS = CS

Stack:

    SP = 0x7C00


-------------------------------------------------------------------------------
Load remaining kernel image
-------------------------------------------------------------------------------

Disk Layout

Sector 0
+----------------+
| bootsect       |
+----------------+

Sector 1..N
+----------------+
| setup code     |
+----------------+

Remaining sectors
+----------------+
| compressed     |
| kernel         |
+----------------+


Memory Layout

0x90000
+----------------+
| bootsect copy  |
+----------------+

0x90200
+----------------+
| setup code     |
+----------------+

0x10000
+----------------+
| compressed      |
| kernel image    |
+----------------+


bootsect jumps to setup code



===============================================================================
PHASE 3 : SETUP CODE (REAL MODE)
===============================================================================

setup.S

Responsibilities:

    - Detect memory
    - Detect video mode
    - Gather BIOS information
    - Enable A20
    - Build temporary GDT
    - Switch to Protected Mode


Real Mode

        CPU
         |
         v

    20-bit addresses

         |
         | enable A20
         v

    access > 1MB memory


Build GDT

+--------------------+
| NULL descriptor    |
+--------------------+
| CODE segment       |
+--------------------+
| DATA segment       |
+--------------------+


Load GDTR

        lgdt


Enter Protected Mode

        mov CR0
        set PE bit
        far jump


        ljmp CODE_SEL,startup_32


===============================================================================
PHASE 4 : HEAD.S (startup_32)
===============================================================================

Now CPU is running 32-bit protected mode


startup_32:
------------

cld
cli

DS = __KERNEL_DS
ES = __KERNEL_DS
FS = __KERNEL_DS
GS = __KERNEL_DS

Load stack

        lss stack_start,%esp


Memory

+-----------------------+
| user_stack            |
|                       |
|                       |
+-----------------------+
           ^
           |
          ESP



===============================================================================
A20 CHECK
===============================================================================


Address 0x000000

+------------------+
| test value       |
+------------------+

Address 0x100000

+------------------+
| compare value    |
+------------------+


If A20 disabled:

        0x100000 wraps to 0x000000

        BAD


If A20 enabled:

        0x000000 != 0x100000

        GOOD


Loop:

        write 0x000000

        compare 0x100000

        continue only if different



===============================================================================
CLEAR BSS
===============================================================================


Before

+----------------------+
| uninitialized data   |
| random garbage       |
+----------------------+


Code:

    rep stosb


After

+----------------------+
| 0                    |
| 0                    |
| 0                    |
+----------------------+



===============================================================================
DECOMPRESS KERNEL
===============================================================================

call decompress_kernel()


Compressed Image

0x10000

+--------------------------------------+
| gzip compressed kernel               |
+--------------------------------------+


decompress_kernel()

            |
            |
            v


+--------------------------------------+
| decompressed kernel                  |
+--------------------------------------+

Usually destination:

        0x00100000
          (1MB)



===============================================================================
NORMAL CASE
===============================================================================


decompress_kernel()

        |
        v

return 0


Code:

    ljmp __KERNEL_CS,__PHYSICAL_START


Jump:

        EIP = __PHYSICAL_START

Usually:

        0x00100000


Memory

0x00100000
+--------------------------------------+
| Linux kernel                         |
| start_kernel()                       |
+--------------------------------------+


Execution continues there.



===============================================================================
HIGH-LOADED CASE
===============================================================================


decompress_kernel()

        |
        v

return != 0


Need relocation


Copy move routine


Current Location

+---------------------------+
| move_routine_start        |
+---------------------------+


Copied To

0x1000

+---------------------------+
| move routine              |
+---------------------------+


Jump

    ljmp __KERNEL_CS,0x1000


Run relocation routine

        |
        v

Move kernel into final location


0x00100000

+--------------------------------------+
| final kernel image                   |
+--------------------------------------+


Jump again

        ljmp __KERNEL_CS,__PHYSICAL_START



===============================================================================
FINAL FLOW
===============================================================================


BIOS
 |
 v
bootsect.S @ 0x7C00
 |
 | load setup
 | load compressed kernel
 |
 v
setup.S @ 0x90200
 |
 | detect hardware
 | enable A20
 | build GDT
 | enter protected mode
 |
 v
head.S (startup_32)
 |
 | setup segments
 | setup stack
 | verify A20
 | clear BSS
 |
 v
decompress_kernel()
 |
 +---------------------+
 |                     |
 | normal              | high-loaded
 |                     |
 v                     v
jump to            relocate kernel
kernel             then jump
 |                     |
 +----------+----------+
            |
            v
     __PHYSICAL_START
            |
            v
      start_kernel()
            |
            v
       Linux Kernel
===============================================================================

This is the classic Linux 2.x compressed-kernel boot path from BIOS all the way to `start_kernel()`.

