============================================================
LINUX x86-64 MODULE LOADER HELPERS
File: arch/x86_64/kernel/module.c
============================================================

PURPOSE
------------------------------------------------------------

This file contains x86-64 architecture-specific support for
loading Linux kernel modules.

A kernel module is an ELF object file loaded into the running
kernel, for example:

    insmod my_driver.ko
    modprobe e1000e

Generic module loading is handled elsewhere.

This file handles x86-64-specific parts:

    - allocate executable module memory
    - free module memory
    - apply ELF relocations
    - handle alternatives patching
    - handle SMP lock patching
    - handle module BUG table setup/cleanup

============================================================
HIGH LEVEL FLOW
============================================================

userspace
    |
    v
insmod module.ko
    |
    v
sys_init_module()
    |
    v
generic module loader
    |
    +--> allocate module memory
    +--> copy ELF sections
    +--> resolve symbols
    +--> apply relocations
    +--> finalize architecture-specific code
    |
    v
module init function runs

============================================================
WHY MODULE RELOCATION IS NEEDED
============================================================

A .ko file is not linked at its final runtime address.

Example:

    module code expects symbol foo

But actual kernel address is:

    foo = 0xffffffff80201234

So loader must patch instructions/data inside the module.

This process is called:

    relocation

============================================================
1. module_alloc()
============================================================

Purpose:

    Allocate executable virtual memory for module text/data.

Code path:

    module_alloc(size)

Flow:

    if size == 0:
        return NULL

    size = PAGE_ALIGN(size)

    if size > MODULES_LEN:
        return NULL

    area = __get_vm_area(size,
                         VM_ALLOC,
                         MODULES_VADDR,
                         MODULES_END)

    return __vmalloc_area(area,
                          GFP_KERNEL,
                          PAGE_KERNEL_EXEC)

Meaning:

    Allocate memory from the special module virtual address range.

============================================================
MODULE ADDRESS RANGE
============================================================

Modules live in a special kernel virtual area:

    MODULES_VADDR  --->  MODULES_END

ASCII:

Kernel Virtual Address Space

+-----------------------------+
| kernel text/data            |
+-----------------------------+
| vmalloc area                |
+-----------------------------+
| module area                 |
|   module A                  |
|   module B                  |
|   module C                  |
+-----------------------------+

Why special range?

    On x86-64, some relative calls/jumps have limited range.
    Modules must be close enough to kernel text for PC-relative
    relocations to work.

============================================================
PAGE_KERNEL_EXEC
============================================================

Module memory must be executable.

So mapping uses:

    PAGE_KERNEL_EXEC

because module .text contains CPU instructions.

Modern kernels are stricter and separate:

    module text
    module rodata
    module data

But this older code is simpler.

============================================================
2. module_free()
============================================================

Purpose:

    Free module memory.

Code:

    vfree(module_region)

If module is unloaded:

    rmmod my_driver

then its memory is returned.

Comment:

    FIXME: If module_region == mod->init_region,
    trim exception table entries.

Meaning:

    old init-only module area may have exception table entries
    that should be removed/trimmed after init finishes.

============================================================
3. module_frob_arch_sections()
============================================================

Function:

    module_frob_arch_sections(...)

Returns:

    0

Meaning:

    x86-64 does not need special preprocessing of ELF sections
    at this stage.

Generic module loader can continue normally.

============================================================
4. ELF BACKGROUND
============================================================

Kernel modules are ELF files.

Important pieces:

    ELF header
    Section headers
    Symbol table
    String table
    Relocation sections

Example sections:

    .text
    .data
    .rodata
    .rela.text
    .rela.data
    .altinstructions
    .smp_locks
    __bug_table

============================================================
SYMBOL EXAMPLE
============================================================

Module C code:

    printk("hello\n");

Compiler emits:

    call printk

But address of printk is unknown when .ko is built.

So ELF contains relocation:

    "patch this call instruction with address of printk"

============================================================
5. apply_relocate_add()
============================================================

Most important function.

Purpose:

    Apply ELF64 RELA relocations.

Prototype:

    int apply_relocate_add(
        Elf64_Shdr *sechdrs,
        const char *strtab,
        unsigned int symindex,
        unsigned int relsec,
        struct module *me
    )

RELA means relocation entries include explicit addend:

    value = symbol_value + relocation_addend

============================================================
REL vs RELA
============================================================

REL:

    addend is stored at relocation target location

RELA:

    addend is stored inside relocation entry

x86-64 uses RELA.

That is why:

    apply_relocate() returns -ENOSYS

and:

    apply_relocate_add() is implemented.

============================================================
6. apply_relocate_add() FLOW
============================================================

For each relocation entry:

    rel[i]

Find location to patch:

    loc = section_to_patch_base + rel[i].r_offset

Find symbol:

    sym = symbol_table + ELF64_R_SYM(rel[i].r_info)

Compute value:

    val = sym->st_value + rel[i].r_addend

Then switch on relocation type:

    R_X86_64_64
    R_X86_64_32
    R_X86_64_32S
    R_X86_64_PC32

============================================================
7. loc MEANING
============================================================

Code:

    loc = sechdrs[target_section].sh_addr + r_offset

Meaning:

    loc is the exact memory address inside the loaded module
    where the loader must write the relocated value.

Example:

    module .text loaded at 0xffffffff88000000

    relocation offset = 0x120

    loc = 0xffffffff88000120

Patch happens there.

============================================================
8. sym MEANING
============================================================

Code:

    sym = symbol_table + ELF64_R_SYM(r_info)

Symbol may refer to:

    - kernel symbol
    - module-local symbol
    - exported function
    - global variable

Example:

    printk
    kmalloc
    init_task
    my_local_function

By this point, undefined symbols have already been resolved.

So:

    sym->st_value

contains final runtime address.

============================================================
9. R_X86_64_64
============================================================

Absolute 64-bit relocation.

Code:

    *(u64 *)loc = val;

Meaning:

    write full 64-bit address/value.

Example:

    data pointer:

        .quad printk

becomes:

        0xffffffff80123456

Use when full address is stored.

============================================================
10. R_X86_64_32
============================================================

Unsigned 32-bit relocation.

Code:

    *(u32 *)loc = val;

Then check:

    if (val != *(u32 *)loc)
        overflow

Meaning:

    value must fit in 32 bits unsigned.

If not:

    relocation overflow

============================================================
11. R_X86_64_32S
============================================================

Signed 32-bit relocation.

Code:

    *(s32 *)loc = val;

Then check:

    if ((s64)val != *(s32 *)loc)
        overflow

Meaning:

    value must fit in signed 32-bit range.

Important for kernel code model.

============================================================
12. R_X86_64_PC32
============================================================

PC-relative 32-bit relocation.

Code:

    val -= (u64)loc;
    *(u32 *)loc = val;

Meaning:

    Store relative offset from current instruction/location.

Example:

    call printk

Machine code contains:

    E8 xx xx xx xx

The xx bytes are:

    target - current_location

ASCII:

loc:
    call relative_offset

relative_offset = printk_address - loc

============================================================
IMPORTANT NOTE ABOUT CALL RELATIVE ADDRESS
------------------------------------------------------------

Real x86 call displacement is usually relative to the next
instruction, not the address of the displacement field itself.

The ELF relocation addend usually accounts for this.

So this simple formula works with compiler/linker-generated
addends.

============================================================
13. OVERFLOW ERROR
============================================================

If relocation cannot fit:

    overflow in relocation type ...

Then message:

    likely not compiled with -mcmodel=kernel

Why?

x86-64 kernel code uses special code model.

Kernel modules must be compiled so references/calls fit the
expected addressing model.

If compiled wrong, addresses may require relocations that cannot
fit in 32-bit signed PC-relative fields.

============================================================
14. -mcmodel=kernel BACKGROUND
============================================================

x86-64 has different code models:

    small
    kernel
    medium
    large

Kernel code lives in high virtual addresses:

    0xffffffff....

The compiler must generate addressing suitable for kernel space.

For kernel modules, wrong code model may produce relocations
that overflow.

============================================================
15. apply_relocate()
============================================================

Function:

    int apply_relocate(...)

Returns:

    -ENOSYS

Prints:

    non add relocation not supported

Meaning:

    x86-64 kernel module loader expects RELA relocations,
    not REL relocations.

============================================================
16. module_finalize()
============================================================

Called after module is loaded and relocated.

Purpose:

    Do final x86-specific patching.

It searches section names:

    .text
    .altinstructions
    .smp_locks

Then handles:

    alternatives
    SMP lock patching
    bug table

============================================================
17. .altinstructions BACKGROUND
============================================================

Linux has runtime instruction patching.

Example:

    If CPU supports feature X:
        use faster instruction

    else:
        use older safe instruction

This is called:

    alternatives

Example idea:

    generic instruction:

        old sequence

    optimized instruction:

        new sequence

At boot/module load, kernel patches code depending on CPU features.

============================================================
18. apply_alternatives()
============================================================

If module has:

    .altinstructions

then:

    apply_alternatives(aseg, aseg + alt->sh_size)

This patches module code just like built-in kernel code.

Example:

    replace NOPs with optimized instructions

or:

    replace LOCK-prefixed sequence

depending on CPU feature.

============================================================
19. .smp_locks BACKGROUND
============================================================

Old kernels optimized LOCK prefixes.

On SMP:

    lock addl ...

is needed for atomicity across CPUs.

On UP:

    lock prefix may be unnecessary overhead.

Section:

    .smp_locks

records locations of lock prefixes.

This lets kernel patch them depending on SMP state.

============================================================
20. alternatives_smp_module_add()
============================================================

If module has .smp_locks and .text:

    alternatives_smp_module_add(
        me,
        me->name,
        lseg,
        lseg + locks->sh_size,
        tseg,
        tseg + text->sh_size
    )

Meaning:

    Register module's SMP lock patch sites.

Later, if CPU/SMP state requires patching, kernel knows where
module lock prefixes are.

============================================================
21. module_bug_finalize()
============================================================

Called at end of module_finalize().

Purpose:

    Register module BUG/WARN locations.

Kernel macros:

    BUG()
    WARN_ON()

emit entries into special bug table.

When module loads, these entries must be registered so kernel can
decode faults and warnings correctly.

============================================================
22. module_arch_cleanup()
============================================================

Called during module unload.

Flow:

    alternatives_smp_module_del(mod)

        remove SMP alternatives info

    module_bug_cleanup(mod)

        unregister BUG table info

This prevents stale pointers after module memory is freed.

============================================================
FULL MODULE LOAD FLOW
============================================================

insmod hello.ko
    |
    v
generic module loader
    |
    +--> parse ELF
    |
    +--> module_alloc()
    |       |
    |       +--> allocate executable vmalloc memory
    |
    +--> copy sections into memory
    |
    +--> resolve symbols
    |
    +--> apply_relocate_add()
    |       |
    |       +--> patch addresses/calls/data
    |
    +--> module_finalize()
    |       |
    |       +--> apply alternatives
    |       +--> register SMP lock patch sites
    |       +--> register BUG table
    |
    +--> call module init function

============================================================
FULL MODULE UNLOAD FLOW
============================================================

rmmod hello
    |
    v
module exit function
    |
    v
module_arch_cleanup()
    |
    +--> remove alternatives info
    +--> remove bug table info
    |
    v
module_free()
    |
    +--> vfree(module memory)

============================================================
MENTAL MODEL
============================================================

A kernel module starts as a relocatable ELF object:

    hello.ko

It cannot run directly.

The kernel module loader must turn it into live kernel code:

    allocate executable memory
    copy sections
    fix addresses
    patch CPU alternatives
    register metadata
    call init

This file is the x86-64 part of that process.

============================================================
ONE-LINE SUMMARY
============================================================

This file allocates executable memory for x86-64 kernel
modules, applies ELF64 RELA relocations, finalizes CPU
alternative/SMP/BUG metadata, and cleans everything up when
the module is unloaded.
============================================================
