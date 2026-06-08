===============================================================================
x86_64 EARLY C BOOT PREPARATION
File: arch/x86_64/kernel/head64.c
===============================================================================

PURPOSE
=======

head.S brings the CPU into 64-bit long mode.

head64.c is the first early C code that prepares the kernel enough to call:

    start_kernel()

So the flow is:

    head.S
      |
      v
    x86_64_start_kernel()
      |
      v
    start_kernel()


===============================================================================
BIG PICTURE
===============================================================================

startup_64 in head.S
      |
      v
x86_64_start_kernel(real_mode_data)
      |
      +--> clear .bss
      |
      +--> install early IDT
      |
      +--> load IDT
      |
      +--> early_printk("Kernel alive")
      |
      +--> switch page tables
      |
      +--> setup PDA pointers
      |
      +--> initialize boot CPU PDA
      |
      +--> copy boot parameters
      |
      +--> mark CPU0 online
      |
      v
start_kernel()


===============================================================================
WHY THIS FILE EXISTS
===============================================================================

head.S does CPU mode setup:

    64-bit mode
    CR0 / CR3 / CR4 / EFER
    temporary stack
    temporary GDT
    dummy GS

But before generic kernel code can run, C code must prepare:

    .bss
    IDT
    page tables
    per-CPU PDA
    boot command line
    boot CPU state


That is head64.c.


===============================================================================
FUNCTION: clear_bss()
===============================================================================

Code:

    memset(__bss_start, 0,
           __bss_stop - __bss_start);


Purpose:

    Clear the kernel BSS section.


BSS contains uninitialized global/static variables.

Example:

    static int x;

The compiler expects:

    x == 0

But bootloader does not necessarily zero it.

So kernel clears BSS manually.


Important comment:

    Do not add printk here.

Why?

    printk relies on PDA.
    PDA is not initialized yet.


Flow:

Kernel Image
    |
    +--> .text
    +--> .data
    +--> .bss  ---> must be zeroed


===============================================================================
FUNCTION: copy_bootdata()
===============================================================================

Purpose:

    Copy bootloader/real-mode boot parameters into kernel-owned memory.

Input:

    real_mode_data

This pointer comes from head.S:

    movl %esi, %edi

So:

    RDI = real_mode_data


===============================================================================
BOOT PARAMETER COPY
===============================================================================

Code:

    memcpy(x86_boot_params, real_mode_data, BOOT_PARAM_SIZE);


Meaning:

    Copy full boot parameter block.


This contains things like:

    memory info
    video info
    initrd info
    command-line pointer
    loader info


===============================================================================
COMMAND LINE COPY
===============================================================================

Linux needs the kernel command line.

Example:

    root=/dev/sda1 earlyprintk=serial console=ttyS0


There are two formats:

------------------------------------------------------------------------------
New command-line pointer
------------------------------------------------------------------------------

Offset:

    NEW_CL_POINTER = 0x228

If present:

    new_data = *(int *)(x86_boot_params + 0x228)


------------------------------------------------------------------------------
Old command-line format
------------------------------------------------------------------------------

If new pointer is missing, check:

    OLD_CL_MAGIC_ADDR = 0x90020
    OLD_CL_MAGIC      = 0xA33F

If valid:

    command line address =
        OLD_CL_BASE_ADDR + OLD_CL_OFFSET


Then:

    memcpy(saved_command_line, command_line, COMMAND_LINE_SIZE);


===============================================================================
copy_bootdata() FLOW
===============================================================================

copy_bootdata(real_mode_data)
      |
      +--> copy boot params into x86_boot_params
      |
      +--> read new command-line pointer
      |
      +--> if missing:
      |       |
      |       +--> check old command-line magic
      |       +--> compute old command-line address
      |
      +--> copy command line into saved_command_line


===============================================================================
FUNCTION: x86_64_start_kernel()
===============================================================================

This is the main function in this file.

Called from:

    head.S startup_64


Prototype:

    void x86_64_start_kernel(char *real_mode_data)


===============================================================================
STEP 1 : CLEAR BSS
===============================================================================

Code:

    clear_bss();


Why first?

Because global variables must be zero before they are used.

Also:

    early IDT setup uses global data.


===============================================================================
STEP 2 : INSTALL EARLY IDT HANDLERS
===============================================================================

Code:

    for (i = 0; i < IDT_ENTRIES; i++)
        set_intr_gate(i, early_idt_handler);


Meaning:

    For all 256 interrupt vectors, install early_idt_handler.


Why?

If an exception happens before real traps are initialized,
the kernel can at least print something and halt.

Example:

    page fault during early boot
    invalid opcode
    general protection fault


Flow:

Exception
    |
    v
early_idt_handler
    |
    v
early_printk()
    |
    v
halt


===============================================================================
STEP 3 : LOAD IDT
===============================================================================

Code:

    lidt idt_descr


Now CPU uses the IDT table.

Before this:

    exception handling is unsafe.

After this:

    early exceptions go to early_idt_handler.


===============================================================================
STEP 4 : EARLY PRINT
===============================================================================

Code:

    early_printk("Kernel alive\n");


This confirms:

    CPU reached early C code.


This uses early console from early_printk.c.


===============================================================================
STEP 5 : SWITCH PAGE TABLES
===============================================================================

Code:

    memcpy(init_level4_pgt,
           boot_level4_pgt,
           PTRS_PER_PGD * sizeof(pgd_t));

    movq __pa_symbol(&init_level4_pgt), %cr3


Meaning:

    Copy early boot PML4 into init_level4_pgt.

Then reload CR3 to use init_level4_pgt.


Why?

head.S used:

    boot_level4_pgt

Now C code switches to:

    init_level4_pgt


Later memory initialization will extend/modify init_level4_pgt.


===============================================================================
PAGE TABLE FLOW
===============================================================================

head.S:

    CR3 = boot_level4_pgt

head64.c:

    init_level4_pgt = copy of boot_level4_pgt
    CR3 = init_level4_pgt

Later:

    paging_init()
    pagetable_init()
    mem_init()

expand final mappings.


===============================================================================
STEP 6 : SETUP PDA ARRAY
===============================================================================

Code:

    for (i = 0; i < NR_CPUS; i++)
        cpu_pda(i) = &boot_cpu_pda[i];


PDA = Per-CPU Data Area.

Older x86_64 kernels used PDA through GS base.

PDA stores:

    current task
    kernel stack
    irq stack
    CPU number
    per-CPU offsets


Flow:

CPU0 -> boot_cpu_pda[0]
CPU1 -> boot_cpu_pda[1]
CPU2 -> boot_cpu_pda[2]


===============================================================================
STEP 7 : INITIALIZE BOOT CPU PDA
===============================================================================

Code:

    pda_init(0);


Meaning:

    Initialize PDA for CPU0.


This sets up proper GS/PDA state for the boot CPU.

After this, code depending on PDA becomes safer.

Examples:

    current
    in_interrupt()
    per-cpu access


===============================================================================
STEP 8 : COPY BOOT DATA
===============================================================================

Code:

    copy_bootdata(real_mode_data);


Copies:

    boot parameters
    kernel command line


This data is needed later by setup code.


===============================================================================
STEP 9 : MARK CPU0 ONLINE
===============================================================================

Under CONFIG_SMP:

    cpu_set(0, cpu_online_map);


Meaning:

    Boot CPU is online.


At this point only CPU0 is running.

Other CPUs are brought up later by SMP code.


===============================================================================
STEP 10 : CALL GENERIC KERNEL
===============================================================================

Code:

    start_kernel();


This enters generic Linux boot.

After this point, architecture-independent initialization begins.


===============================================================================
COMPLETE FLOW
===============================================================================

head.S startup_64
      |
      v
x86_64_start_kernel(real_mode_data)
      |
      +--> clear_bss()
      |
      +--> setup early IDT
      |
      +--> lidt
      |
      +--> early_printk("Kernel alive")
      |
      +--> copy boot_level4_pgt to init_level4_pgt
      |
      +--> reload CR3
      |
      +--> setup cpu_pda[] array
      |
      +--> pda_init(0)
      |
      +--> copy boot params and command line
      |
      +--> mark CPU0 online
      |
      v
start_kernel()


===============================================================================
RELATION TO head.S
===============================================================================

head.S does:

    CPU mode transition

    32-bit protected mode
        ->
    64-bit long mode

    setup temporary stack
    setup temporary GDT
    setup dummy GS
    jump to x86_64_start_kernel


head64.c does:

    early C environment preparation

    clear BSS
    setup early IDT
    switch page tables
    initialize PDA
    copy boot data
    call start_kernel


Together:

    head.S  = CPU becomes 64-bit capable
    head64.c = C kernel becomes usable


===============================================================================
RELATION TO early_printk.c
===============================================================================

head64.c calls:

    early_printk("Kernel alive\n");


If earlyprintk is enabled, you see:

    Kernel alive


This proves:

    startup_64 succeeded
    stack works
    early C code is running


===============================================================================
RELATION TO entry.S
===============================================================================

head64.c sets an early IDT.

entry.S later provides the real runtime entries for:

    system calls
    interrupts
    exceptions


Early phase:

    all IDT entries -> early_idt_handler


Later phase:

    IDT entries -> page_fault, general_protection, nmi, etc.


===============================================================================
RELATION TO e820.c
===============================================================================

head64.c copies boot parameters.

e820.c later reads memory map information from those boot parameters.

Flow:

bootloader
    |
    v
real_mode_data
    |
    v
copy_bootdata()
    |
    v
x86_boot_params
    |
    v
setup_memory_region()
    |
    v
e820 map


===============================================================================
KEY IDEA
===============================================================================

head64.c is the bridge between assembly bootstrap and normal kernel C code.

It performs the minimum C-level setup needed before start_kernel():

    - zero BSS
    - install early exception handlers
    - switch to init page tables
    - initialize PDA/per-CPU basics
    - copy boot parameters
    - mark boot CPU online


After x86_64_start_kernel() calls start_kernel(),
the generic Linux boot process begins.
===============================================================================
