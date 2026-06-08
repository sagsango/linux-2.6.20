```text
===============================================================================
FILE: arch/x86_64/kernel/suspend_asm.S
PURPOSE: SOFTWARE SUSPEND / HIBERNATION CPU STATE SAVE AND IMAGE RESTORE
KERNEL: Linux 2.6.x x86-64
===============================================================================

BACKGROUND
===============================================================================

This file is part of Linux software suspend / hibernation.

Hibernation is different from normal sleep.

Normal suspend-to-RAM:

    RAM stays powered.
    CPU sleeps.
    Resume continues from RAM.

Hibernation / suspend-to-disk:

    RAM image is saved to disk.
    Machine may fully power off.
    Later boot reloads old RAM image.
    Kernel restores old memory image.
    Execution continues from the old kernel state.

This file handles the most dangerous architecture-specific part:

    saving CPU register state before hibernation

and

    restoring the old kernel memory image during resume

===============================================================================
BIG DIFFERENCE: SUSPEND vs HIBERNATION
===============================================================================

Suspend-to-RAM:

    CPU state saved
    RAM remains alive
    resume firmware wakes CPU

------------------------------------------------------------

Hibernate:

    RAM copied to disk
    power may go off
    later a new kernel boots
    old RAM image is read back
    memory pages are copied back to original locations
    CPU registers are restored
    old kernel continues

===============================================================================
WHY THIS FILE IS ASSEMBLY
===============================================================================

During image restore, the kernel is doing something very dangerous:

    overwriting the currently running kernel image
    with the saved old kernel image

That means:

    current stack may be overwritten
    current code may be overwritten
    current data variables may be overwritten

Therefore, the resume copy code cannot safely use:

    normal C stack
    normal local variables
    normal kernel memory assumptions

The file comment says this clearly:

    swsusp_arch_resume may not use any stack,
    nor any variable that is not NoSave during copying pages.

Reason:

    what is stack in the current kernel may be data in the restored image.

Overwriting your own stack while running is fatal.

===============================================================================
HIGH LEVEL HIBERNATION FLOW
===============================================================================

Suspend side:

    running kernel
        |
        v
    swsusp_arch_suspend()
        |
        v
    save registers
        |
        v
    swsusp_save()
        |
        v
    write memory image to disk
        |
        v
    power off

Resume side:

    fresh kernel boots
        |
        v
    reads hibernation image from disk
        |
        v
    builds restore page list
        |
        v
    restore_image()
        |
        v
    copy saved pages back to original addresses
        |
        v
    restore registers
        |
        v
    return as if swsusp_arch_suspend() returned 0

===============================================================================
IMPORTANT CONCEPT: TWO KERNEL IMAGES
===============================================================================

During hibernation resume, there are effectively two kernels:

    1. The fresh boot kernel

       This kernel booted normally after power-on.

    2. The saved old kernel image

       This is the memory snapshot from before hibernation.

The fresh kernel's job is to restore the old kernel image.

After restore:

    fresh kernel disappears

    old kernel continues

===============================================================================
ASCII MODEL
===============================================================================

Before restore:

    RAM contains fresh boot kernel

        +----------------------+
        | fresh kernel text    |
        | fresh kernel data    |
        | fresh stack          |
        +----------------------+

Saved image pages are elsewhere.

During restore:

    restore_image()
        |
        v
    copy saved pages back

After restore:

        +----------------------+
        | old kernel text      |
        | old kernel data      |
        | old stacks           |
        +----------------------+

Then execution returns into the restored old kernel.

===============================================================================
PART 1: swsusp_arch_suspend
===============================================================================

Entry:

    ENTRY(swsusp_arch_suspend)

Purpose:

    save CPU register state

Registers saved:

    RSP
    RAX
    RBX
    RCX
    RDX
    RBP
    RSI
    RDI
    R8-R15
    EFLAGS

Then:

    call swsusp_save

Then:

    ret

===============================================================================
WHY SAVE REGISTERS?
===============================================================================

When the system resumes, Linux wants execution to continue as if
suspend returned normally.

That requires restoring:

    stack pointer

    general registers

    flags

Otherwise the old kernel execution context would be broken.

===============================================================================
SAVE CONTEXT FLOW
===============================================================================

swsusp_arch_suspend()
    |
    +--> save RSP
    +--> save RAX-R15
    +--> save RBP
    +--> save EFLAGS
    |
    +--> call swsusp_save()
    |
    +--> ret

===============================================================================
WHAT swsusp_save() DOES
===============================================================================

swsusp_save() is C code elsewhere.

It creates the hibernation image.

Conceptually:

    freeze tasks
    snapshot memory
    write image to disk/swap
    prepare power off

This assembly file only saves CPU state and later restores memory.

===============================================================================
PART 2: restore_image
===============================================================================

Entry:

    ENTRY(restore_image)

Purpose:

    copy saved pages back to their original physical locations

This is the dangerous "overwrite running kernel" phase.

===============================================================================
RESTORE FLOW
===============================================================================

restore_image()
    |
    +--> switch to temporary page tables
    |
    +--> flush TLB
    |
    +--> walk restore_pblist
    |
    +--> copy each saved page to original address
    |
    +--> switch back to original/restored page tables
    |
    +--> flush TLB again
    |
    +--> restore saved registers
    |
    +--> return to restored kernel context

===============================================================================
WHY TEMPORARY PAGE TABLES?
===============================================================================

During restore, normal kernel mappings may be overwritten.

So restore code uses temporary safe mappings.

Code:

    temp_level4_pgt

This is temporary top-level page table.

It allows the restore code to keep running while memory is being copied.

===============================================================================
SWITCH TO TEMP PAGE TABLES
===============================================================================

Code:

    movq temp_level4_pgt(%rip), %rax
    subq $__PAGE_OFFSET, %rax
    movq %rax, %cr3

Meaning:

    load CR3 with physical address of temporary PML4

CR3 controls page table root.

Changing CR3 changes address translation.

===============================================================================
WHY subtract __PAGE_OFFSET?
===============================================================================

temp_level4_pgt is a kernel virtual address.

CR3 needs a physical address.

In the direct map:

    physical = virtual - __PAGE_OFFSET

So:

    temp_level4_pgt - __PAGE_OFFSET

gives physical address.

===============================================================================
TLB FLUSH WITH PGE HANDLING
===============================================================================

Code disables PGE:

    clear CR4.PGE

reloads CR3:

    mov %cr3, %cr3

then restores CR4.

PGE = Page Global Enable.

Global pages are not normally flushed by CR3 reload.

So to flush everything, including global mappings:

    disable PGE
    reload CR3
    re-enable PGE

===============================================================================
WHY GLOBAL TLB ENTRIES MATTER
===============================================================================

Kernel mappings may be marked global.

If global TLB entries survive restore, CPU might use stale translations
from the fresh kernel.

That would be catastrophic.

So the code flushes global TLB entries explicitly.

===============================================================================
RESTORE PAGE LIST
===============================================================================

Variable:

    restore_pblist

This points to a linked list of page backup entries.

Each entry has:

    pbe_address

        source page address

    pbe_orig_address

        destination/original page address

    pbe_next

        next page backup entry

===============================================================================
PAGE COPY LOOP
===============================================================================

Loop:

    load pbe

    source = pbe_address

    destination = pbe_orig_address

    copy 512 qwords

    go to next pbe

Why 512 qwords?

    512 * 8 = 4096 bytes

One page.

===============================================================================
COPY DIAGRAM
===============================================================================

Saved copy page
    |
    | 4096 bytes
    v
Original physical page

===============================================================================
EXAMPLE
===============================================================================

restore_pblist entry:

    pbe_address      = 0x12000000
    pbe_orig_address = 0xffffffff80200000

Copy:

    0x12000000 -> 0xffffffff80200000

This restores one old kernel page.

===============================================================================
WHY NO NORMAL STACK DURING COPYING?
===============================================================================

Suppose current stack is at page X.

The restore list says:

    copy saved old data over page X

If function uses current stack while copying:

    push return address
    local variable
    saved register

could be overwritten mid-copy.

Therefore restore code avoids depending on stack during the page copy loop.

It uses registers only:

    rdx = current pbe
    rsi = source
    rdi = destination
    rcx = counter

===============================================================================
DONE: SWITCH BACK TO ORIGINAL PAGE TABLES
===============================================================================

After all pages copied:

    leaq init_level4_pgt(%rip), %rax
    subq $__START_KERNEL_map, %rax
    movq %rax, %cr3

This switches back to the restored kernel's normal page tables.

Then TLB is flushed again, including global entries.

===============================================================================
WHY init_level4_pgt?
===============================================================================

After the old image is restored, the kernel page table root is:

    init_level4_pgt

The code switches to it so the restored kernel address space is active.

===============================================================================
RESTORE DS
===============================================================================

Code:

    movl $24, %eax
    movl %eax, %ds

This reloads the data segment selector.

Even in x86-64, some segment registers still need sane values.

===============================================================================
RESTORE SAVED REGISTERS
===============================================================================

After memory is restored, CPU registers are restored from saved_context:

    RSP
    RBP
    RBX
    RCX
    RDX
    RSI
    RDI
    R8-R15
    EFLAGS

RAX is not restored.

Instead:

    xorq %rax, %rax

So restored suspend path returns 0.

===============================================================================
WHY RAX RETURNS 0?
===============================================================================

Function return values on x86-64 are in RAX.

On resume, Linux wants:

    swsusp_arch_suspend() returns 0

meaning:

    "we are now resumed"

So:

    RAX = 0

===============================================================================
FINAL RET
===============================================================================

The restored RSP points to the old saved stack.

The final:

    ret

returns into the restored old kernel call chain.

From the old kernel's point of view:

    swsusp_arch_suspend()

just returned.

===============================================================================
COMPLETE RESUME CONTROL FLOW
===============================================================================

Fresh boot kernel
    |
    v
load hibernation image
    |
    v
restore_image()
    |
    +--> switch to temp page tables
    |
    +--> copy saved pages to original addresses
    |
    +--> switch to restored page tables
    |
    +--> restore CPU registers
    |
    v
ret
    |
    v
old kernel resumes after swsusp_arch_suspend()

===============================================================================
COMPARISON WITH KEXEC relocate_kernel.S
===============================================================================

Both files copy kernel images.

But direction differs.

-------------------------------------------------------------------------------

kexec relocate_kernel.S:

    old kernel
        |
        v
    copy new kernel into place
        |
        v
    jump to new kernel

-------------------------------------------------------------------------------

swsusp restore_image:

    fresh boot kernel
        |
        v
    copy old hibernated kernel into place
        |
        v
    return into old kernel

===============================================================================
COMPARISON WITH NORMAL REBOOT
===============================================================================

Normal reboot:

    reset hardware
    firmware runs
    bootloader loads kernel

Hibernate resume:

    fresh kernel starts
    restores old memory image
    jumps/returns into old kernel state

Kexec:

    old kernel directly starts new kernel without firmware

===============================================================================
DANGEROUS PARTS
===============================================================================

This code must not:

    allocate memory

    call normal C functions during restore loop

    use overwritten stack

    rely on stale TLB entries

    leave global TLB entries alive

    restore wrong CR3

Any mistake can cause instant crash.

===============================================================================
MENTAL MODEL
===============================================================================

Think of hibernation resume as:

    time travel by memory replacement

The fresh kernel is only a temporary loader.

It restores the old world by copying every saved page back.

Then it restores CPU registers and returns into the old execution stack.

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

This assembly file implements the x86-64 hibernation CPU-state save and
image-restore trampoline: it saves all important registers before suspend,
then during resume switches to temporary page tables, copies saved memory
pages back to their original locations without relying on a normal stack,
flushes all TLB state including global entries, restores the saved CPU
context, sets RAX to zero, and returns into the restored pre-hibernation
kernel execution path.
===============================================================================
```

