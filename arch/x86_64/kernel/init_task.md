===============================================================================
INITIAL TASK / INIT_MM / INIT_TSS
File: arch/x86_64/kernel/init_task.c
===============================================================================

PURPOSE
=======

This file defines the first task in the system:

    init_task

This is the ancestor of all Linux tasks.

Before fork(), before PID 1, before userspace, Linux needs one task_struct
to represent the currently running kernel context.

That task is:

    init_task


===============================================================================
BIG PICTURE
===============================================================================

Boot CPU starts running kernel code
        |
        v
init_task exists statically
        |
        v
start_kernel()
        |
        v
kernel_init thread
        |
        v
PID 1 userspace init


init_task is not allocated dynamically.

It is built into the kernel image.


===============================================================================
IMPORTANT OBJECTS
===============================================================================

init_fs
-------

Initial filesystem context.

Contains:

    root directory
    current working directory
    umask


init_files
----------

Initial file descriptor table.

Before userspace, there are no real open files.


init_signals
------------

Initial signal state.


init_sighand
------------

Initial signal handler table.


init_mm
-------

Initial memory descriptor.

Represents kernel address space.

Exported as:

    EXPORT_SYMBOL(init_mm)


init_task
---------

First task_struct.

The boot CPU runs as this task.


init_thread_union
-----------------

Initial kernel stack + thread_info storage.


init_tss
--------

Per-CPU Task State Segment.


===============================================================================
INIT_MM
===============================================================================

Code:

    struct mm_struct init_mm = INIT_MM(init_mm);


Purpose:

    Initial memory map used by kernel threads.


Important:

Kernel threads do not have normal userspace memory.

They usually use:

    active_mm = &init_mm


Diagram:

init_task
    |
    +--> mm        = NULL
    |
    +--> active_mm = &init_mm


Meaning:

    No userspace address space,
    but still has kernel mappings.


===============================================================================
INIT_THREAD_UNION
===============================================================================

Code:

union thread_union init_thread_union
    __attribute__((__section__(".data.init_task"))) =
        { INIT_THREAD_INFO(init_task) };


This contains:

    thread_info
    kernel stack


Classic layout:

+-----------------------------+
| thread_info                 |
+-----------------------------+
|                             |
| kernel stack                |
|                             |
| grows downward              |
+-----------------------------+


Why special alignment?

Comment says it must be 8192-byte aligned.

Reason:

    older Linux finds thread_info by masking stack pointer.


Example:

    current stack pointer
          |
          v
    align down to THREAD_SIZE
          |
          v
    thread_info


===============================================================================
INIT_TASK
===============================================================================

Code:

    struct task_struct init_task = INIT_TASK(init_task);


This statically creates the first task_struct.


All later tasks are allocated dynamically during fork:

    fork()
      |
      v
    alloc_task_struct()
      |
      v
    slab allocator


But init_task must exist before slab allocator exists.


===============================================================================
WHY INIT_TASK IS SPECIAL
===============================================================================

Normal task:

    allocated by fork.c
    has dynamic kernel stack
    has copied task_struct


init_task:

    statically allocated
    exists before memory allocators
    exists before scheduler fully starts
    represents boot CPU execution


Flow:

CPU begins kernel C code
      |
      v
current == init_task


===============================================================================
PER-CPU TSS
===============================================================================

Code:

DEFINE_PER_CPU(struct tss_struct, init_tss)
    ____cacheline_internodealigned_in_smp = INIT_TSS;


TSS = Task State Segment.


Old x86:

    one TSS per task

Linux x86_64:

    one TSS per CPU


Comment says:

    Threads are completely "soft" on Linux,
    no more per-task TSS's.


Meaning:

Linux does context switching in software.

Hardware task switching is not used.


===============================================================================
WHAT TSS IS USED FOR ON x86_64
===============================================================================

Even though Linux does not use hardware task switching,
x86_64 still needs TSS for:

    - RSP0 kernel stack pointer
    - IST stacks
    - I/O bitmap


Important:

When CPU transitions from user mode to kernel mode through interrupt,
CPU can load kernel stack from TSS.RSP0.


IST = Interrupt Stack Table.

Used for:

    NMI
    Double Fault
    Machine Check
    Debug stack


===============================================================================
PER-CPU TSS LAYOUT
===============================================================================

CPU0:

    init_tss[CPU0]

CPU1:

    init_tss[CPU1]

CPU2:

    init_tss[CPU2]


Each CPU has its own TSS because each CPU has its own kernel stacks.


===============================================================================
CACHELINE ALIGNMENT
===============================================================================

Code uses:

    ____cacheline_internodealigned_in_smp


Why?

TSS is CPU-local.

If CPU0 and CPU1 TSS shared one cacheline, writes could cause cacheline
bouncing between CPUs.

Alignment avoids false sharing.


===============================================================================
orig_ist
===============================================================================

Code:

    DEFINE_PER_CPU(struct orig_ist, orig_ist);


Purpose:

    Store original IST values for debugging.


No special alignment needed because it is rarely accessed.


===============================================================================
COMPLETE BOOT RELATION
===============================================================================

head.S
    |
    +--> uses init_thread_union for initial stack


head64.c
    |
    +--> pda_init(0)
    |
    +--> current task becomes init_task


start_kernel()
    |
    +--> scheduler initializes around init_task


fork/init
    |
    +--> creates kernel_init
    |
    +--> eventually starts PID 1


===============================================================================
TASK BOOTSTRAP FLOW
===============================================================================

Kernel image contains:

    init_task
    init_thread_union
    init_mm
    init_tss


Boot:

CPU enters kernel
      |
      v
uses init_thread_union stack
      |
      v
current points to init_task
      |
      v
start_kernel()
      |
      v
scheduler initialized
      |
      v
kernel creates more tasks
      |
      v
normal process model begins


===============================================================================
KEY IDEA
===============================================================================

This file provides the statically allocated seed objects for the process
system.

Before Linux can allocate or fork any task, it needs:

    - one task_struct
    - one kernel stack
    - one thread_info
    - one mm_struct
    - one per-CPU TSS

That initial seed is init_task.

Everything else grows from it.
===============================================================================
