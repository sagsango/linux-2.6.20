Here is the **IDE-style one-file note** for `arch/x86_64/kernel/ldt.c`:

```text
============================================================
Linux x86_64 LDT Handling
File: arch/x86_64/kernel/ldt.c
============================================================

GOAL
------------------------------------------------------------
This file implements modify_ldt() syscall support.

LDT = Local Descriptor Table

Used mainly for:
    - old 32-bit compatibility programs
    - Wine / DOSEMU style userspace
    - custom segment descriptors
    - legacy x86 segmentation support

On x86-64, normal Linux does NOT use LDT much.
Flat memory model is used instead.

============================================================
1. BIG PICTURE
============================================================

Userspace
   |
   v
sys_modify_ldt()
   |
   +--> read_ldt()
   |
   +--> write_ldt()
   |
   +--> read_default_ldt()
   |
   v
mm->context.ldt
   |
   v
load_LDT()
   |
   v
CPU LDTR register updated


============================================================
2. WHERE LDT IS STORED
============================================================

Each process mm_struct has:

    mm->context.ldt
    mm->context.size
    mm->context.sem

Meaning:

    ldt  = pointer to LDT memory
    size = number of LDT entries
    sem  = lock protecting LDT changes

So LDT is per-process address-space, not global.

============================================================
3. alloc_ldt()
============================================================

Purpose:

    Allocate or grow LDT table.

Flow:

    if requested size <= current size
        return

    round mincount to 512 entries

    if size > PAGE_SIZE
        use vmalloc()
    else
        use kmalloc()

    copy old LDT to new LDT

    zero new entries

    update:
        pc->ldt
        pc->size

    optionally reload LDT on CPUs

Important:

    LDT_ENTRY_SIZE = 8 bytes

Each descriptor is 8 bytes:

    entry_1 = low 32 bits
    entry_2 = high 32 bits

============================================================
4. SMP LDT RELOAD
============================================================

Function:

    flush_ldt()

On SMP:

    If one process updates its LDT,
    CPUs running the same mm need to reload LDTR.

Flow:

    current CPU:
        load_LDT(pc)

    other CPUs using same mm:
        smp_call_function(flush_ldt)

ASCII:

CPU0 writes LDT
   |
   +--> load_LDT() on CPU0
   |
   +--> send IPI to other CPUs
            |
            v
        flush_ldt()
            |
            v
        load_LDT()

============================================================
5. init_new_context()
============================================================

Called when creating new mm context.

Example:

    fork()

Flow:

    initialize mm->context.sem
    mm->context.size = 0

    if parent has LDT:
        copy parent's LDT

Important comment:

    "we do not have to muck with descriptors here,
     that is done in switch_mm() as needed"

Meaning:

    This only copies memory.
    Actual CPU LDT register loading happens during mm switch.

============================================================
6. destroy_context()
============================================================

Called when mm is destroyed.

Flow:

    if mm has LDT:
        free ldt memory

    if size > PAGE_SIZE:
        vfree()
    else:
        kfree()

Important:

    It does NOT touch CPU LDTR here.

Why?

    Because this mm is already not active
    in the current thread.

============================================================
7. read_ldt()
============================================================

Used when:

    sys_modify_ldt(func = 0)

Flow:

    if no LDT:
        return 0

    clamp bytecount to max LDT size

    lock mm->context.sem

    copy LDT to userspace

    unlock

    zero-fill remaining user buffer if needed

Purpose:

    Let userspace read current LDT entries.

============================================================
8. read_default_ldt()
============================================================

Used when:

    sys_modify_ldt(func = 2)

On x86-64:

    default LDT is all zeros

So:

    clear_user(ptr, bytecount)

============================================================
9. write_ldt()
============================================================

Most important function.

Used by:

    sys_modify_ldt(func = 1)
    sys_modify_ldt(func = 0x11)

Flow:

    copy struct user_desc from userspace

    validate entry_number

    validate contents

    lock mm->context.sem

    grow LDT if needed

    convert user_desc into descriptor words

    install descriptor

    unlock

============================================================
10. user_desc
============================================================

Userspace gives:

    struct user_desc

It contains logical fields:

    entry_number
    base_addr
    limit
    seg_32bit
    contents
    read_exec_only
    limit_in_pages
    seg_not_present
    useable

Kernel converts it into raw x86 descriptor:

    entry_1 = LDT_entry_a(&ldt_info)
    entry_2 = LDT_entry_b(&ldt_info)

Descriptor layout:

    8 bytes total

    low  32 bits -> entry_1
    high 32 bits -> entry_2

============================================================
11. Installing LDT Entry
============================================================

Code:

    lp = (__u32 *)((entry_number << 3) + mm->context.ldt)

Why << 3?

    entry_number * 8

Because each LDT entry is 8 bytes.

Then:

    *lp     = entry_1
    *(lp+1) = entry_2

ASCII:

LDT table:

entry 0:  [ 8 bytes ]
entry 1:  [ 8 bytes ]
entry 2:  [ 8 bytes ]
entry 3:  [ 8 bytes ]

entry_number = 3

offset = 3 * 8 = 24 bytes

============================================================
12. Clearing an LDT Entry
============================================================

If userspace passes:

    base_addr == 0
    limit == 0

and entry is empty:

    entry_1 = 0
    entry_2 = 0

This clears the LDT descriptor.

============================================================
13. oldmode vs new mode
============================================================

write_ldt(..., oldmode)

func = 1:

    write_ldt(ptr, bytecount, 1)

func = 0x11:

    write_ldt(ptr, bytecount, 0)

Old mode has extra restrictions.

Example:

    if oldmode:
        entry_2 &= ~(1 << 20)

This clears the usable bit for old-style callers.

============================================================
14. sys_modify_ldt()
============================================================

Main syscall entry.

Code:

    sys_modify_ldt(int func, void __user *ptr, unsigned long bytecount)

Switch:

    func = 0
        read_ldt()

    func = 1
        write_ldt(oldmode = 1)

    func = 2
        read_default_ldt()

    func = 0x11
        write_ldt(oldmode = 0)

Unknown func:

    return -ENOSYS

============================================================
15. FULL FLOW: WRITE LDT
============================================================

userspace
   |
   v
modify_ldt(0x11, &desc, sizeof(desc))
   |
   v
sys_modify_ldt()
   |
   v
write_ldt()
   |
   +--> copy_from_user()
   |
   +--> validate descriptor
   |
   +--> alloc_ldt() if table too small
   |
   +--> build raw descriptor
   |
   +--> write into mm->context.ldt
   |
   +--> load_LDT()
   |
   v
CPU now uses updated LDT

============================================================
16. FULL FLOW: READ LDT
============================================================

userspace
   |
   v
modify_ldt(0, buffer, size)
   |
   v
sys_modify_ldt()
   |
   v
read_ldt()
   |
   +--> lock
   +--> copy mm->context.ldt to userspace
   +--> unlock
   |
   v
userspace receives raw LDT entries

============================================================
17. WHY LDT EXISTS
============================================================

Old x86 used segmentation heavily.

Address calculation:

    virtual address = segment base + offset

Segment descriptor stores:

    base
    limit
    type
    privilege
    present bit

In modern x86-64 Linux:

    segmentation is mostly disabled / flat

But compatibility still exists.

============================================================
18. GDT vs LDT
============================================================

GDT:

    Global Descriptor Table
    system-wide / CPU-wide descriptors

LDT:

    Local Descriptor Table
    per-process custom descriptors

ASCII:

CPU
 |
 +--> GDTR -> GDT
 |
 +--> LDTR -> LDT of current process

============================================================
19. IMPORTANT LOCKING
============================================================

Lock:

    mm->context.sem

Protects:

    mm->context.ldt
    mm->context.size

Used in:

    init_new_context()
    read_ldt()
    write_ldt()

============================================================
20. IMPORTANT MEMORY ALLOCATION
============================================================

Small LDT:

    kmalloc()

Large LDT:

    vmalloc()

Reason:

    Large LDT may need virtually contiguous memory,
    not physically contiguous memory.

============================================================
21. ONE-LINE SUMMARY
============================================================

modify_ldt() lets userspace read/write per-process x86
LDT entries; this file allocates, copies, installs, reloads,
and frees those descriptor tables.
============================================================
```

