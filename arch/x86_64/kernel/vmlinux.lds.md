```text
===============================================================================
FILE: arch/x86_64/kernel/vmlinux.lds.S
PURPOSE: LINKER SCRIPT THAT DEFINES THE FINAL x86-64 KERNEL IMAGE LAYOUT
KERNEL: Linux 2.6.x
===============================================================================

BACKGROUND
===============================================================================

This is not normal C code.

This is a linker script.

It tells the linker:

    where kernel code goes
    where kernel data goes
    where init code goes
    where per-cpu data goes
    where exception tables go
    where vsyscall page goes
    what symbols mark section boundaries
    what is discarded
    what physical address each section is loaded at
    what virtual address each section runs at

In simple words:

    this file creates the final memory layout of vmlinux.

===============================================================================
WHY LINKER SCRIPT IS IMPORTANT
===============================================================================

When the kernel is compiled, each .o file has sections:

    .text
    .data
    .bss
    .init.text
    .init.data
    .rodata
    .data.percpu
    __ex_table

The linker must combine all of them into one final executable:

    vmlinux

This file tells exactly how.

Without this script, the kernel would not know:

    where _text starts
    where _end is
    where init memory begins
    where exception table lives
    where jiffies lives
    where vsyscall page is mapped

===============================================================================
BIG PICTURE
===============================================================================

Object files:

    head.o
    setup.o
    traps.o
    time.o
    sched.o
    mm.o

Each contains:

    .text
    .data
    .bss
    .init.text
    .rodata

Linker script:

    collect all .text together
    collect all .data together
    collect all .bss together
    align important sections
    define important symbols

Output:

    vmlinux

===============================================================================
KEY IDEA: VIRTUAL ADDRESS VS PHYSICAL LOAD ADDRESS
===============================================================================

x86-64 kernel runs at high virtual address:

    __START_KERNEL_map

Example conceptually:

    ffffffff80000000

But it is loaded physically much lower:

    0x100000 or similar

So the linker script constantly uses:

    virtual address = high kernel address

    load address = virtual address - LOAD_OFFSET

===============================================================================
LOAD_OFFSET
===============================================================================

Defined:

    #define LOAD_OFFSET __START_KERNEL_map

Used like:

    AT(ADDR(.text) - LOAD_OFFSET)

Meaning:

    section virtual address is high

but

    physical load address is virtual minus kernel mapping offset.

===============================================================================
EXAMPLE
===============================================================================

Suppose:

    ADDR(.text) = ffffffff80100000
    LOAD_OFFSET = ffffffff80000000

Then:

    load address = 0x00100000

So bootloader loads it at physical 1MB,
but kernel executes at high virtual address.

===============================================================================
ENTRY POINT
===============================================================================

ENTRY(phys_startup_64)

This tells linker:

    kernel entry symbol is phys_startup_64

Defined:

    phys_startup_64 = startup_64 - LOAD_OFFSET

So entry is the physical address version of startup_64.

===============================================================================
WHY phys_startup_64?
===============================================================================

Early boot starts before normal high virtual mapping is fully active.

So boot code needs a physical entry point.

startup_64 is linked at high address.

phys_startup_64 gives its physical address.

===============================================================================
PROGRAM HEADERS
===============================================================================

PHDRS defines ELF load segments:

    text      PT_LOAD R_E
    data      PT_LOAD RWE
    user      PT_LOAD RWE
    data.init PT_LOAD RWE
    note      PT_NOTE R__

These control how ELF loaders see the kernel image.

===============================================================================
.text SECTION
===============================================================================

Starts at:

    _text = .

Contains:

    .bootstrap.text

        must be first

    functionlist

        hot functions grouped together

    .text

        normal kernel code

    SCHED_TEXT

        scheduler hot text

    LOCK_TEXT

        lock routines

    KPROBES_TEXT

        kprobe-safe code

    .fixup

        exception fixup code

===============================================================================
WHY .bootstrap.text FIRST?
===============================================================================

Early boot code must be at predictable location.

The CPU starts at the entry path.

So bootstrapping code comes first.

===============================================================================
WHY functionlist?
===============================================================================

Comment says hot functions are grouped onto same hugepage entry.

Purpose:

    performance

If frequently executed code is near each other:

    better TLB/cache locality

===============================================================================
.text.lock
===============================================================================

Contains out-of-line lock code.

Old kernel lock slow paths may be separated from hot path.

This improves cache layout.

===============================================================================
_etext
===============================================================================

Symbol:

    _etext = .

Marks end of kernel text.

Used by:

    memory protection
    kallsyms
    module/core code
    debugging

===============================================================================
EXCEPTION TABLE
===============================================================================

Symbols:

    __start___ex_table
    __stop___ex_table

Section:

    __ex_table

Purpose:

    kernel fault recovery

Example:

    copy_from_user() faults

Exception table maps:

    faulting instruction -> fixup instruction

traps.c uses:

    search_exception_tables(regs->rip)

to recover.

===============================================================================
RODATA
===============================================================================

Macro:

    RODATA

Collects read-only data.

Examples:

    const strings
    const tables
    jump tables

Usually mapped read-only later.

===============================================================================
BUG_TABLE
===============================================================================

Contains metadata for BUG()/WARN() sites.

When invalid opcode UD2 triggers BUG,
kernel can find source location and metadata.

===============================================================================
.data SECTION
===============================================================================

Contains writable initialized data.

Example:

    global variables with initial value

    int x = 5;

Symbol:

    _edata

marks end of normal data.

===============================================================================
CACHELINE ALIGNED DATA
===============================================================================

Section:

    .data.cacheline_aligned

Purpose:

    variables that must avoid false sharing

Aligned to:

    CONFIG_X86_L1_CACHE_BYTES

Example:

    hot per-system locks/counters

===============================================================================
READ MOSTLY DATA
===============================================================================

Section:

    .data.read_mostly

Purpose:

    variables written rarely, read often

Putting them together improves cache behavior.

Example:

    configuration flags
    CPU feature masks

===============================================================================
VSYSCALL REGION
===============================================================================

This is one of the most interesting parts.

Old x86-64 Linux used a fixed vsyscall page at:

    VSYSCALL_ADDR = -10MB

Meaning near top of user virtual address space.

Purpose:

    allow userspace to call fast time functions without a syscall

Examples:

    gettimeofday()
    time()
    getcpu()

===============================================================================
WHY VSYSCALL EXISTS
===============================================================================

Normal syscall:

    user -> kernel -> user

expensive.

For time reads, kernel can expose read-only time data and a small code page.

Then userspace can read time quickly.

===============================================================================
VSYSCALL LAYOUT
===============================================================================

Sections:

    .vsyscall_0
    .vsyscall_1
    .vsyscall_2
    .vsyscall_3

Placed at fixed offsets:

    +0
    +1024
    +2048
    +3072

Inside one page.

===============================================================================
VSYSCALL DATA EXPORTS
===============================================================================

The linker script places special variables near vsyscall:

    xtime_lock
    vxtime
    vgetcpu_mode
    sys_tz
    sysctl_vsyscall
    xtime
    jiffies

These are used by old vsyscall code to compute time quickly.

===============================================================================
WHY SPECIAL SYMBOL ASSIGNMENTS?
===============================================================================

Example:

    xtime_lock = VVIRT(.xtime_lock);

The physical storage lives near kernel data.

But vsyscall-visible virtual address differs.

The linker creates symbols that point to the correct virtual alias.

===============================================================================
init_task SECTION
===============================================================================

Section:

    .data.init_task

Aligned to:

    8192

Contains:

    init_task

This is the first task_struct / kernel stack setup.

Alignment matters because thread_info/task stack layout depends on it.

===============================================================================
PAGE ALIGNED DATA
===============================================================================

Section:

    .data.page_aligned

Contains objects needing page alignment.

Examples:

    page tables
    stacks
    special per-CPU structures

===============================================================================
SMP ALTERNATIVES
===============================================================================

Sections:

    .smp_altinstructions
    .smp_locks
    .smp_altinstr_replacement

Purpose:

    runtime patching depending on SMP/UP state.

Example:

    on UP kernel, lock prefixes may be patched out

    on SMP, lock prefixes remain

This improves performance on uniprocessor systems.

===============================================================================
INIT SECTIONS
===============================================================================

Beginning:

    __init_begin

End:

    __init_end

Contains:

    .init.text
    .init.data
    .init.setup
    .initcall.init
    .con_initcall.init
    .init.ramfs
    .data.percpu

These sections are needed only during boot.

After boot:

    free_initmem()

can release them.

===============================================================================
.init.text
===============================================================================

Functions marked:

    __init

go here.

Example:

    time_init()
    trap_init()
    setup_arch()

They run once at boot.

After boot, memory can be freed.

===============================================================================
.init.data
===============================================================================

Data marked:

    __initdata

goes here.

Used only during boot.

===============================================================================
.init.setup
===============================================================================

Contains boot parameter handlers.

Example:

    __setup("nohpet", nohpet_setup)
    early_param("oops", oops_setup)

Kernel command line parsing walks:

    __setup_start -> __setup_end

===============================================================================
.initcall.init
===============================================================================

Contains initcall function pointers.

Macros like:

    core_initcall()
    fs_initcall()
    device_initcall()

place entries here.

Kernel later walks this range and calls them.

===============================================================================
.con_initcall.init
===============================================================================

Console initcalls.

Used to initialize console output early enough for printk.

===============================================================================
SECURITY_INIT
===============================================================================

Security subsystem initialization hooks.

Example:

    LSM initialization

===============================================================================
ALTERNATIVES
===============================================================================

Sections:

    .altinstructions
    .altinstr_replacement

Used for CPU feature-based patching.

Example:

    if CPU supports fast instruction, patch code

    otherwise use generic fallback

This is used heavily on x86.

===============================================================================
.exit.text and .exit.data
===============================================================================

Exit sections are not discarded at link time here.

Comment says:

    needed because .altinstructions and .eh_frame may reference them

They may be discarded at runtime instead.

===============================================================================
INITRAMFS
===============================================================================

Symbols:

    __initramfs_start
    __initramfs_end

Section:

    .init.ramfs

Contains built-in initramfs archive.

Used to create early root filesystem.

===============================================================================
PER-CPU SECTION
===============================================================================

Symbols:

    __per_cpu_start
    __per_cpu_end

Section:

    .data.percpu

This is the per-cpu template.

setup64.c later copies this template once per CPU.

Example:

    per_cpu variable template
        |
        +--> CPU0 copy
        +--> CPU1 copy
        +--> CPU2 copy

===============================================================================
NOSAVE SECTION
===============================================================================

Symbols:

    __nosave_begin
    __nosave_end

Section:

    .data_nosave

Used by hibernation.

Memory in this range is not saved/restored.

Important for suspend/resume code that must survive image copying.

===============================================================================
BSS SECTION
===============================================================================

Symbols:

    __bss_start
    __bss_stop

Contains zero-initialized data.

Example:

    static int x;

No bytes need to be stored in image for zeros.

Kernel clears BSS during boot.

===============================================================================
_end
===============================================================================

Symbol:

    _end = .

Marks end of kernel image.

Used for:

    memory reservation
    bootmem setup
    finding free memory after kernel

===============================================================================
DISCARD SECTION
===============================================================================

Discarded at link time:

    .exitcall.exit
    .eh_frame

These are not needed in final kernel image.

===============================================================================
DEBUG SECTIONS
===============================================================================

STABS_DEBUG

DWARF_DEBUG

Debug info sections.

Used for debugging symbols, not runtime logic.

===============================================================================
COMPLETE MEMORY LAYOUT
===============================================================================

High virtual kernel image:

    __START_KERNEL
        |
        v
    .text
    .text.lock
    __ex_table
    .rodata
    bug table
    .data
    .data.cacheline_aligned
    .data.read_mostly

    vsyscall page mapping area

    .data.init_task
    .data.page_aligned
    smp alternatives

    __init_begin
        .init.text
        .init.data
        .init.setup
        .initcall.init
        .init.ramfs
        .data.percpu
    __init_end

    __nosave_begin
        .data_nosave
    __nosave_end

    .bss

    _end

===============================================================================
RELATION TO BOOT CODE
===============================================================================

head.S needs:

    startup_64
    phys_startup_64
    _text
    page tables
    init_level4_pgt

This linker script decides where those symbols live.

===============================================================================
RELATION TO setup.c
===============================================================================

setup.c reserves memory:

    _text -> _end

because that is kernel image.

It also uses:

    __init_begin
    __init_end

for init memory.

===============================================================================
RELATION TO traps.c
===============================================================================

traps.c uses exception table:

    __start___ex_table
    __stop___ex_table

for fault fixups.

===============================================================================
RELATION TO time.c / vsyscall.c
===============================================================================

time.c exports:

    xtime
    vxtime
    jiffies

This linker script places aliases for vsyscall usage.

===============================================================================
RELATION TO setup64.c
===============================================================================

setup64.c uses:

    __per_cpu_start
    __per_cpu_end

to allocate per-cpu copies.

===============================================================================
RELATION TO suspend.c
===============================================================================

suspend/hibernate uses:

    __nosave_begin
    __nosave_end

to avoid saving memory that must remain stable during restore.

===============================================================================
MENTAL MODEL
===============================================================================

C files define code and variables.

Assembly files define startup code.

This linker script decides:

    final address
    final order
    final alignment
    physical load location
    virtual runtime location
    boundary symbols

It is the blueprint of vmlinux memory.

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/vmlinux.lds.S is the x86-64 kernel linker blueprint:
it lays out vmlinux in high virtual memory while assigning physical load
addresses, defines all major kernel section boundaries, places text/data/init/
per-cpu/nosave/vsyscall/exception-table regions, aligns performance-critical
areas, and exports symbols that boot, memory management, exception handling,
timekeeping, per-cpu setup, and hibernation rely on.
===============================================================================
```

