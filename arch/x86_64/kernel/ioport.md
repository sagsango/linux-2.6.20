```text
===============================================================================
I/O PORT PERMISSION CONTROL
File: arch/x86_64/kernel/ioport.c
===============================================================================

PURPOSE
=======

This file implements two system calls:

    ioperm()
    iopl()

They control whether a user process can directly access x86 I/O ports.

I/O ports are used by old-style hardware access instructions:

    inb
    outb
    inw
    outw


===============================================================================
BACKGROUND: WHAT ARE I/O PORTS?
===============================================================================

x86 has two major ways to access devices:

1. MMIO
-------

Device registers are mapped into memory address space.

Example:

    *(volatile u32 *)addr = value


2. Port I/O
-----------

Device registers are accessed using special CPU instructions.

Example:

    outb(value, port)
    inb(port)


Port examples:

    0x3f8  -> COM1 serial port
    0x60   -> keyboard controller
    0x64   -> keyboard status/control
    0x20   -> PIC command
    0x21   -> PIC data
    0x40   -> PIT channel 0
    0x43   -> PIT control


===============================================================================
WHY PERMISSION IS NEEDED
===============================================================================

Direct I/O port access is dangerous.

A user program with unrestricted port access could:

    - reprogram interrupt controllers
    - corrupt device state
    - crash the machine
    - bypass kernel drivers
    - interfere with DMA or timers


So Linux only allows it with:

    CAP_SYS_RAWIO


===============================================================================
TWO ACCESS MODELS
===============================================================================

ioperm()
--------

Fine-grained permission.

Allows selected I/O port ranges.

Example:

    allow ports 0x3f8 - 0x3ff


iopl()
------

Coarse-grained privilege.

Changes I/O privilege level in EFLAGS.

Can allow broad access, including ports beyond 0x3ff.


===============================================================================
PART 1: IOPERM
===============================================================================

System call:

    sys_ioperm(unsigned long from,
               unsigned long num,
               int turn_on)


Arguments:

    from
        first port number

    num
        number of ports

    turn_on
        1 = allow access
        0 = deny access


Example:

    ioperm(0x3f8, 8, 1)

allows access to:

    COM1 serial port range


===============================================================================
I/O BITMAP
===============================================================================

x86 TSS contains an I/O permission bitmap.

Each bit controls one I/O port.

Important rule:

    bit = 0  -> access allowed
    bit = 1  -> access denied


This is why code uses:

    set_bitmap(..., !turn_on)


If user asks to turn permission ON:

    turn_on = 1

then bitmap bit becomes:

    0

meaning access allowed.


===============================================================================
BITMAP VIEW
===============================================================================

Port numbers:

    0      1      2      3      ...      0x3f8
    |      |      |      |                 |
    v      v      v      v                 v

I/O bitmap:

    bit0   bit1   bit2   bit3   ...       bit0x3f8


Access rule:

    0 = allowed
    1 = denied


===============================================================================
set_bitmap()
===============================================================================

Function:

    set_bitmap(bitmap, base, extent, new_value)


Purpose:

    Set or clear a range of bits.


Flow:

for i = base to base + extent:
    if new_value:
        set bit i
    else:
        clear bit i


Used by:

    sys_ioperm()


===============================================================================
sys_ioperm() FLOW
===============================================================================

sys_ioperm(from, num, turn_on)
      |
      +--> validate range
      |
      +--> if enabling, require CAP_SYS_RAWIO
      |
      +--> allocate bitmap if first use
      |
      +--> update current thread bitmap
      |
      +--> compute active bitmap size
      |
      +--> copy bitmap into current CPU TSS
      |
      v
return 0


===============================================================================
STEP 1: VALIDATE RANGE
===============================================================================

Code:

    if ((from + num <= from) ||
        (from + num > IO_BITMAP_BITS))
        return -EINVAL;


Checks:

    - overflow
    - range beyond max port number


There are 65536 I/O ports:

    0x0000 - 0xffff


===============================================================================
STEP 2: CHECK CAPABILITY
===============================================================================

Code:

    if (turn_on && !capable(CAP_SYS_RAWIO))
        return -EPERM;


Only privileged processes can gain new I/O permissions.


===============================================================================
STEP 3: LAZY BITMAP ALLOCATION
===============================================================================

Code:

    if (!t->io_bitmap_ptr) {
        bitmap = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);
        memset(bitmap, 0xff, IO_BITMAP_BYTES);
        t->io_bitmap_ptr = bitmap;
        set_thread_flag(TIF_IO_BITMAP);
    }


Meaning:

    Do not allocate 8KB bitmap for every process.

Allocate only when process calls ioperm().


Initial bitmap is all 1s:

    all ports denied


===============================================================================
WHY LAZY ALLOCATION?
===============================================================================

Most tasks never use direct I/O ports.

Allocating bitmap for every task would waste memory.

So Linux delays allocation until first ioperm() call.


===============================================================================
STEP 4: UPDATE THREAD BITMAP
===============================================================================

Code:

    set_bitmap(t->io_bitmap_ptr, from, num, !turn_on);


If turn_on = 1:

    clear bits -> allow access


If turn_on = 0:

    set bits -> deny access


===============================================================================
STEP 5: UPDATE TSS
===============================================================================

Code:

    tss = &per_cpu(init_tss, get_cpu());

    memcpy(tss->io_bitmap,
           t->io_bitmap_ptr,
           bytes_updated);

    put_cpu();


Why TSS?

The CPU checks the current TSS I/O bitmap when user code executes:

    in
    out


The kernel keeps a per-thread copy in:

    current->thread.io_bitmap_ptr

and copies it to current CPU's TSS.


===============================================================================
WHY get_cpu()?
===============================================================================

get_cpu() disables preemption.

Reason:

    We must update the TSS of the CPU we are currently running on.

If task migrated to another CPU during update:

    wrong TSS could be updated


Flow:

get_cpu()
   |
   +--> disable preemption
   |
   +--> get current CPU
   |
   +--> update that CPU's TSS
   |
   +--> put_cpu()


===============================================================================
io_bitmap_max
===============================================================================

The kernel tracks how many bytes of bitmap are meaningful.

Code scans:

    for each long:
        if bitmap word != all ones:
            max_long = i


Because:

    all ones = all ports denied

Only need to copy up to highest word that has some allowed port.


===============================================================================
IOPERM COMPLETE EXAMPLE
===============================================================================

Program:

    ioperm(0x3f8, 8, 1)


Kernel:

    from = 0x3f8
    num  = 8
    turn_on = 1


Flow:

    check CAP_SYS_RAWIO
    allocate bitmap if needed
    clear bits 0x3f8 - 0x3ff
    copy bitmap to TSS


Then userspace can do:

    outb(value, 0x3f8)


===============================================================================
PART 2: IOPL
===============================================================================

System call:

    sys_iopl(unsigned int level,
             struct pt_regs *regs)


Purpose:

    Change I/O privilege level in EFLAGS.


IOPL is stored in EFLAGS bits:

    12 and 13


Values:

    0 = least privilege
    3 = most privilege for user I/O


===============================================================================
WHY IOPL EXISTS
===============================================================================

ioperm() bitmap for all 65536 ports would require about:

    65536 bits = 8192 bytes

per process.


For broad low-level programs, iopl() can grant wider I/O privilege instead.


Comment says:

    sys_iopl has to be used when you want to access IO ports beyond 0x3ff.


===============================================================================
sys_iopl() FLOW
===============================================================================

sys_iopl(level, regs)
      |
      +--> validate level <= 3
      |
      +--> read old IOPL from regs->eflags
      |
      +--> if increasing privilege:
      |       |
      |       +--> require CAP_SYS_RAWIO
      |
      +--> modify EFLAGS IOPL bits
      |
      v
return 0


===============================================================================
EFLAGS IOPL BITS
===============================================================================

Code:

    old = (regs->eflags >> 12) & 3;


Bits:

    EFLAGS[13:12]


Update:

    regs->eflags =
        (regs->eflags & ~0x3000UL) | (level << 12);


Mask:

    0x3000 = bits 12 and 13


===============================================================================
WHY MODIFY pt_regs?
===============================================================================

sys_iopl() changes the saved user EFLAGS on the syscall stack frame.

When syscall returns to userspace:

    iret/sysret restores EFLAGS

So the user process resumes with new IOPL.


Flow:

user calls iopl(3)
      |
      v
sys_iopl()
      |
      v
modify regs->eflags
      |
      v
return to user
      |
      v
user EFLAGS has IOPL=3


===============================================================================
SECURITY RULE
===============================================================================

Lowering privilege:

    allowed

Raising privilege:

    requires CAP_SYS_RAWIO


Code:

    if (level > old) {
        if (!capable(CAP_SYS_RAWIO))
            return -EPERM;
    }


===============================================================================
IOPERM VS IOPL
===============================================================================

ioperm()
--------

    fine-grained
    selected ports
    uses TSS I/O bitmap
    safer


iopl()
------

    coarse-grained
    changes EFLAGS IOPL
    broader privilege
    more dangerous


===============================================================================
RELATION TO TSS
===============================================================================

This file connects directly to:

    init_task.c

which defines per-CPU TSS:

    DEFINE_PER_CPU(struct tss_struct, init_tss)


The TSS contains:

    io_bitmap


sys_ioperm() updates:

    per_cpu(init_tss, cpu).io_bitmap


===============================================================================
RELATION TO entry.S
===============================================================================

entry.S builds pt_regs on syscall entry.

sys_iopl() receives:

    struct pt_regs *regs


It modifies:

    regs->eflags


When entry.S returns to user mode, modified flags take effect.


===============================================================================
RELATION TO USER PROGRAMS
===============================================================================

Programs that may use this:

    X server
    dosemu
    old hardware utilities
    direct serial/parallel port tools
    low-level diagnostics


Modern drivers generally should not use userspace port I/O.

They should use kernel drivers.


===============================================================================
COMPLETE IOPERM FLOW
===============================================================================

User program
      |
      v
ioperm(0x3f8, 8, 1)
      |
      v
sys_ioperm()
      |
      +--> check capability
      |
      +--> allocate bitmap
      |
      +--> clear permission bits
      |
      +--> copy to TSS
      |
      v
return to user
      |
      v
outb() allowed for COM1 ports


===============================================================================
COMPLETE IOPL FLOW
===============================================================================

User program
      |
      v
iopl(3)
      |
      v
sys_iopl()
      |
      +--> check CAP_SYS_RAWIO
      |
      +--> modify saved EFLAGS IOPL bits
      |
      v
return to user
      |
      v
user can execute broader I/O port instructions


===============================================================================
KEY IDEA
===============================================================================

ioport.c controls direct user access to x86 I/O ports.

It uses two mechanisms:

    ioperm:
        per-port bitmap in TSS

    iopl:
        privilege level bits in EFLAGS


Both are dangerous and require CAP_SYS_RAWIO when gaining privilege.

This file is the security gate between userspace and raw hardware I/O ports.
===============================================================================
```

