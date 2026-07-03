```text
FILE: arch/x86_64/mm/mmap.c
TOPIC: Choosing mmap layout for a process

============================================================
1. BIG IDEA
============================================================

arch_pick_mmap_layout() decides where new mmap() regions should start
inside a process virtual address space.

Example:

    shared libraries
    anonymous mappings
    file mappings
    thread stacks
    heap-related mappings

When a process calls:

    mmap(NULL, size, ...)

the kernel must choose a free virtual address.

This function sets the policy for that.

============================================================
2. WHERE IT FITS
============================================================

Process creation:

    fork/exec
        |
        v
    create mm_struct
        |
        v
    arch_pick_mmap_layout(mm)
        |
        v
    mm->mmap_base is selected
        |
        v
    later mmap(NULL, size) uses this base

So this does not map memory immediately.

It prepares the address-selection policy for future mmap calls.

============================================================
3. MM_STRUCT FIELDS USED
============================================================

    mm->mmap_base

Base address where mmap search starts.

------------------------------------------------------------

    mm->get_unmapped_area

Function pointer used to find free virtual space.

------------------------------------------------------------

    mm->unmap_area

Function pointer used when unmapping / organizing VMAs.

============================================================
4. CODE WALK
============================================================

Code:

    void arch_pick_mmap_layout(struct mm_struct *mm)
    {

This function receives the memory descriptor of a process.

------------------------------------------------------------

Code:

    #ifdef CONFIG_IA32_EMULATION
        if (current_thread_info()->flags & _TIF_IA32)
            return ia32_pick_mmap_layout(mm);
    #endif

If this is a 32-bit compatibility process running on a 64-bit kernel,
use the IA32 mmap layout instead.

Why?

32-bit processes have a much smaller virtual address space.

So they need different mmap rules.

Flow:

    is CONFIG_IA32_EMULATION enabled?
        |
        v
    is current task a 32-bit task?
        |
        +-- yes -> use ia32_pick_mmap_layout()
        |
        +-- no  -> continue x86-64 layout

------------------------------------------------------------

Code:

    mm->mmap_base = TASK_UNMAPPED_BASE;

Set default mmap base.

This is the initial starting point for mmap allocations.

Conceptually:

    mmap area begins around TASK_UNMAPPED_BASE

------------------------------------------------------------

Code:

    if (current->flags & PF_RANDOMIZE) {

If process has address-space randomization enabled, randomize mmap base.

PF_RANDOMIZE is used for ASLR.

------------------------------------------------------------

Code:

        unsigned rnd = get_random_int() & 0xfffffff;

Get 28 bits of randomness.

Mask:

    0xfffffff = 28 bits

------------------------------------------------------------

Code:

        mm->mmap_base += ((unsigned long)rnd) << PAGE_SHIFT;

Convert random page count into byte offset.

Since PAGE_SHIFT is usually 12:

    rnd << 12

means:

    rnd * 4096

So randomness is page-aligned.

This preserves valid page alignment for mmap base.

------------------------------------------------------------

Code:

    mm->get_unmapped_area = arch_get_unmapped_area;
    mm->unmap_area = arch_unmap_area;

Set architecture-specific helpers.

Later mmap code will call:

    mm->get_unmapped_area(...)

to find a free address.

============================================================
5. WHY RANDOMIZE MMAP_BASE?
============================================================

Without randomization:

    libc
    ld.so
    mmaped files
    anonymous mappings

may appear at predictable addresses.

With randomization:

    attacker cannot easily predict where mappings are.

This helps defend against exploits that depend on known addresses.

This is part of ASLR:

    Address Space Layout Randomization

============================================================
6. ADDRESS SPACE DIAGRAM
============================================================

Simplified x86-64 userspace:

    low address
    0x0000000000000000
        |
        | program text
        | data/bss
        | heap
        |
        | mmap area starts around mm->mmap_base
        | shared libraries
        | anonymous mappings
        | file mappings
        |
        | stack near top
        v
    high user address

With randomization:

    TASK_UNMAPPED_BASE
        |
        + random page-aligned offset
        |
        v
    mm->mmap_base

============================================================
7. FLOW DIAGRAM
============================================================

    arch_pick_mmap_layout(mm)
        |
        v
    IA32 emulation enabled?
        |
        +-- yes:
        |       current is 32-bit task?
        |           |
        |           +-- yes:
        |                   ia32_pick_mmap_layout(mm)
        |                   return
        |
        v
    mm->mmap_base = TASK_UNMAPPED_BASE
        |
        v
    PF_RANDOMIZE set?
        |
        +-- yes:
        |       rnd = get_random_int() & 0xfffffff
        |       mm->mmap_base += rnd << PAGE_SHIFT
        |
        +-- no:
        |       keep fixed mmap_base
        |
        v
    mm->get_unmapped_area = arch_get_unmapped_area
        |
        v
    mm->unmap_area = arch_unmap_area
        |
        v
    done

============================================================
8. EXAMPLE
============================================================

Assume:

    TASK_UNMAPPED_BASE = 0x0000004000000000
    PAGE_SHIFT         = 12
    rnd                = 0x12345

Then:

    random offset = 0x12345 << 12
                  = 0x12345000

So:

    mm->mmap_base = 0x0000004000000000
                  + 0x0000000012345000
                  = 0x0000004012345000

Future mmap(NULL, size) searches around:

    0x0000004012345000

============================================================
9. WHY 28-BIT RANDOMNESS BECOMES ABOUT 40 BITS
============================================================

Comment says:

    Add 28bit randomness which is about 40bits of address space
    because mmap base has to be page aligned.

Reason:

    random value has 28 bits
    then shifted by PAGE_SHIFT

Usually:

    PAGE_SHIFT = 12

So:

    28 random bits + 12 page-offset bits = 40 address bits

But lower 12 bits are always zero because address is page-aligned.

So entropy is still 28 bits, but it spans a 40-bit byte range.

============================================================
10. IMPORTANT DISTINCTION
============================================================

This function does not allocate memory.

It only sets:

    where mmap search should start
    which functions should perform mmap/unmap layout operations

Actual mapping happens later in functions such as:

    do_mmap()
    get_unmapped_area()
    arch_get_unmapped_area()

============================================================
11. ONE-LINE SUMMARY
============================================================

arch_pick_mmap_layout() selects the mmap base address and mmap helper
functions for a process; for 32-bit tasks it delegates to IA32 logic, and
for 64-bit tasks it optionally randomizes mmap_base for ASLR.
```

