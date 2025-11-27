In Linux 2.6.20, the "subsystems" in the context of the boot/initcall
mechanism refer to groups of kernel initialization functions that run in a
well-defined order during do_initcalls().

These are NOT subsystems like "VFS, scheduler, memory manager" (architectural
subsystems). Instead, they are initcall levels — categories of initialization
routines that the kernel executes in sequence during early boot.

---------------------------------------------------------------------------
All Initcall Levels (Subsystems) in Linux 2.6.20
---------------------------------------------------------------------------

Linux 2.6.20 uses these initcall levels (in order):

Level | Macro                 | Purpose
------+------------------------+--------------------------------------------------------
0     | pure_initcall()       | Very early init, minimal, no dependencies.
1     | core_initcall()       | Core kernel subsystems — scheduler, timers, basic memory.
2     | postcore_initcall()   | After core subsystems but before arch-specific parts.
3     | arch_initcall()       | Architecture-specific initialization (x86, ARM, etc.).
4     | subsys_initcall()     | Drivers that create core kernel subsystems
      |                        | (block layer, net core, input core, tty core, etc.)
5     | fs_initcall()         | Filesystem initialization.
6     | device_initcall()     | Device drivers (PCI, USB, etc.).
7     | late_initcall()       | Runs last; late hardware tasks.

These macros are defined in include/linux/init.h in Linux 2.6.x.

---------------------------------------------------------------------------
Hierarchy as executed by do_initcalls()
---------------------------------------------------------------------------

pure_initcall
   ↓
core_initcall
   ↓
postcore_initcall
   ↓
arch_initcall
   ↓
subsys_initcall
   ↓
fs_initcall
   ↓
device_initcall
   ↓
late_initcall

Equivalent to your notes:

    initcall levels: pure -> core -> postcore -> arch -> subsys -> fs -> device -> late

---------------------------------------------------------------------------
What "subsys" means here
---------------------------------------------------------------------------

subsys_initcall() is used for initialization of kernel subsystem frameworks:

    - networking subsystem (net/)
    - block subsystem (block/)
    - input subsystem (drivers/input)
    - tty subsystem
    - char/block core layers
    - crypto API core
    - kobject/sysfs core
    - module loader core
    - etc.

These initialize frameworks used BY drivers, not the device drivers themselves.

---------------------------------------------------------------------------
Example (Linux 2.6.20)
---------------------------------------------------------------------------

subsys_initcall(blk_dev_init);   // block layer core
subsys_initcall(net_dev_init);   // networking core
subsys_initcall(tty_init);       // tty subsystem

---------------------------------------------------------------------------
Summary
---------------------------------------------------------------------------

When Linux 2.6.20 mentions "subsystems" in the boot process, it refers to
initcall subsystem levels, not architectural components.

The initcall levels are:

1. pure_initcall
2. core_initcall
3. postcore_initcall
4. arch_initcall
5. subsys_initcall
6. fs_initcall
7. device_initcall
8. late_initcall

---------------------------------------------------------------------------

