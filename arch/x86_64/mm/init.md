FILE: linux/init/init.c — IDE STYLE NOTE
PURPOSE: Explain kernel init background, code walk, and boot flow.

============================================================
1. BIG PICTURE
============================================================

init.c is part of the Linux kernel boot path.

After architecture-specific assembly code prepares the CPU, stack,
paging, and basic environment, control reaches C code.

The major flow is:

    bootloader
        |
        v
    arch/x86 boot assembly
        |
        v
    start_kernel()
        |
        v
    kernel initialization
        |
        v
    create kernel threads
        |
        v
    run /sbin/init or systemd
        |
        v
    userspace starts

So init.c is where the kernel moves from:

    "CPU just entered kernel"

to:

    "kernel is ready to start userspace"

============================================================
2. WHY INIT.C EXISTS
============================================================

The kernel cannot immediately run normal programs.

Before userspace can start, the kernel must initialize:

    - CPU
    - memory allocator
    - scheduler
    - interrupts
    - timers
    - process/task system
    - VFS
    - block devices
    - drivers
    - root filesystem
    - init process

So init.c coordinates the early boot sequence.

Think of it as:

    kernel main() + system bootstrap manager

============================================================
3. IMPORTANT FUNCTIONS
============================================================

Typical important functions are:

    start_kernel()
        Main kernel entry in C.

    rest_init()
        Starts the first kernel threads.

    kernel_init()
        Later initialization path.

    kernel_init_freeable()
        Initialization code that can be freed after boot.

    init_post()
        Starts the first userspace init program.

    run_init_process()
        Executes /sbin/init, /etc/init, /bin/init, or /bin/sh.

============================================================
4. HIGH LEVEL FLOW
============================================================

ASCII flow:

    +-------------------------+
    | bootloader              |
    +-------------------------+
                |
                v
    +-------------------------+
    | arch boot assembly      |
    | head.S / startup code   |
    +-------------------------+
                |
                v
    +-------------------------+
    | start_kernel()          |
    +-------------------------+
                |
                v
    +-------------------------+
    | setup memory            |
    | setup scheduler         |
    | setup interrupts        |
    | setup timers            |
    | setup console/logging   |
    +-------------------------+
                |
                v
    +-------------------------+
    | rest_init()             |
    +-------------------------+
                |
                v
       +-------------------+
       | create kthreadd   |
       +-------------------+
                |
                v
       +-------------------+
       | create kernel_init|
       +-------------------+
                |
                v
    +-------------------------+
    | kernel_init()           |
    +-------------------------+
                |
                v
    +-------------------------+
    | mount root filesystem   |
    | initialize drivers      |
    | free __init memory      |
    +-------------------------+
                |
                v
    +-------------------------+
    | exec /sbin/init         |
    | or systemd              |
    +-------------------------+

============================================================
5. START_KERNEL()
============================================================

start_kernel() is like the kernel's C-level main function.

Conceptually:

    void start_kernel(void)
    {
        setup_arch();
        setup_per_cpu_areas();
        sched_init();
        build_all_zonelists();
        page_alloc_init();
        trap_init();
        init_IRQ();
        time_init();
        console_init();
        vfs_caches_init();
        rest_init();
    }

Actual code differs by kernel version, but the idea is the same.

Its job:

    - Initialize architecture
    - Initialize memory
    - Initialize scheduler
    - Initialize interrupts
    - Initialize timers
    - Initialize kernel subsystems
    - Start init threads

============================================================
6. REST_INIT()
============================================================

rest_init() is called near the end of start_kernel().

Its job is to create the first important kernel threads.

Important tasks:

    - create kernel_init thread
    - create kthreadd thread
    - make boot CPU enter idle loop

Flow:

    start_kernel()
        |
        v
    rest_init()
        |
        +--> kernel_thread(kernel_init)
        |
        +--> kernel_thread(kthreadd)
        |
        +--> cpu_idle()

So after rest_init(), the original boot task usually becomes idle task.

============================================================
7. KERNEL_INIT()
============================================================

kernel_init() continues initialization after scheduler and basic kernel
threading are ready.

It does things that may need sleeping, scheduling, or kernel threads.

Conceptually:

    kernel_init()
    {
        wait_for_kthreadd();
        kernel_init_freeable();
        free_initmem();
        run_init_process("/sbin/init");
    }

This is where the kernel prepares to start userspace.

============================================================
8. KERNEL_INIT_FREEABLE()
============================================================

This part contains boot-only initialization.

It may do:

    - smp initialization
    - driver initialization
    - initcalls
    - mount root filesystem
    - prepare namespaces
    - open /dev/console

Why "freeable"?

Because after boot, this code is no longer needed.

The kernel can free memory marked:

    __init
    __initdata

So boot-only code/data does not waste RAM forever.

============================================================
9. INITCALLS
============================================================

Many kernel subsystems register initialization functions using macros:

    early_initcall()
    core_initcall()
    postcore_initcall()
    arch_initcall()
    subsys_initcall()
    fs_initcall()
    device_initcall()
    late_initcall()

These are called in order.

Flow:

    do_initcalls()
        |
        +--> core init
        +--> memory/fs init
        +--> device init
        +--> driver init
        +--> late init

Example:

    device_initcall(e1000_init_module);

This means the driver init function runs during boot.

============================================================
10. STARTING USERSPACE
============================================================

Eventually kernel tries to execute init.

Common order:

    /sbin/init
    /etc/init
    /bin/init
    /bin/sh

Modern systems usually run:

    /sbin/init -> systemd

Flow:

    kernel_init()
        |
        v
    init_post()
        |
        v
    run_init_process("/sbin/init")
        |
        v
    execve("/sbin/init", argv, envp)

After this, process PID 1 becomes userspace init.

============================================================
11. WHY PID 1 IS SPECIAL
============================================================

PID 1 is the first userspace process.

It is responsible for:

    - starting services
    - mounting filesystems
    - managing daemons
    - reaping orphan processes
    - bringing system to usable state

If PID 1 dies, the system usually panics.

============================================================
12. DETAILED BOOT FLOW DIAGRAM
============================================================

    BIOS/UEFI
        |
        v
    Bootloader
        |
        v
    Load kernel image
        |
        v
    x86 real/protected/long mode setup
        |
        v
    head.S
        |
        v
    start_kernel()
        |
        +--> setup_arch()
        |       |
        |       +--> parse boot params
        |       +--> setup memory map
        |       +--> setup CPU features
        |
        +--> setup_command_line()
        |
        +--> setup_per_cpu_areas()
        |
        +--> build_all_zonelists()
        |
        +--> page_alloc_init()
        |
        +--> trap_init()
        |
        +--> mm_init()
        |
        +--> sched_init()
        |
        +--> init_IRQ()
        |
        +--> time_init()
        |
        +--> console_init()
        |
        +--> vfs_caches_init()
        |
        +--> rest_init()
                |
                +--> create kernel_init thread
                |
                +--> create kthreadd thread
                |
                +--> boot CPU becomes idle task

    kernel_init thread
        |
        +--> wait for kthreadd
        |
        +--> kernel_init_freeable()
        |       |
        |       +--> do_basic_setup()
        |       |       |
        |       |       +--> do_initcalls()
        |       |
        |       +--> mount root filesystem
        |
        +--> free_initmem()
        |
        +--> run_init_process("/sbin/init")
                |
                v
            userspace PID 1

============================================================
13. MEMORY VIEW
============================================================

During boot:

    +---------------------------+
    | permanent kernel text     |
    +---------------------------+
    | permanent kernel data     |
    +---------------------------+
    | __init text               |  used only during boot
    +---------------------------+
    | __init data               |  used only during boot
    +---------------------------+

After boot:

    +---------------------------+
    | permanent kernel text     |
    +---------------------------+
    | permanent kernel data     |
    +---------------------------+
    | freed memory              |  old __init area reused
    +---------------------------+

That is why many init functions are marked:

    __init

Example:

    static int __init my_driver_init(void)

============================================================
14. WHY INIT IS SPLIT
============================================================

The kernel cannot do everything inside start_kernel() because early boot
has restrictions.

Early phase:

    - scheduler may not fully work
    - interrupts may be disabled
    - memory allocators may be limited
    - no normal process context yet

Later phase:

    - kernel threads exist
    - scheduler works
    - blocking/sleeping is possible
    - drivers can initialize safely

So:

    start_kernel()
        early, delicate, low-level

    kernel_init()
        later, more normal kernel context

============================================================
15. SIMPLE MENTAL MODEL
============================================================

    start_kernel()
        = build the kernel world

    rest_init()
        = start kernel threads

    kernel_init()
        = finish setup

    run_init_process()
        = start userspace

============================================================
16. ONE-LINE SUMMARY
============================================================

init.c is the kernel's boot coordinator: it initializes core kernel
subsystems, starts kernel threads, frees boot-only memory, mounts the
root filesystem, and finally executes userspace init as PID 1.
