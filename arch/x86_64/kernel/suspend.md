```text
===============================================================================
FILE: arch/x86_64/kernel/suspend.c
PURPOSE: x86-64 PROCESSOR STATE SAVE/RESTORE FOR SUSPEND/HIBERNATION
KERNEL: Linux 2.6.x
===============================================================================

BACKGROUND
===============================================================================

This file is the C-side partner of:

    arch/x86_64/kernel/suspend_asm.S

Together they implement the architecture-specific part of suspend and
hibernation resume.

The assembly file did the dangerous part:

    save general registers
    copy hibernation image pages
    restore registers

This C file does the structured CPU-state part:

    save descriptor tables
    save segment registers
    save FS/GS base MSRs
    save control registers
    restore them later
    rebuild temporary mappings for hibernation restore

===============================================================================
WHY THIS FILE EXISTS
===============================================================================

When a CPU resumes from suspend/hibernate, many architectural registers may
not be valid anymore.

Linux must restore:

    GDT
    IDT
    task register
    segment registers
    FS/GS bases
    CR0/CR2/CR3/CR4/CR8
    syscall MSRs
    TSS descriptor
    LDT
    debug registers
    MTRRs
    FPU state

Generic suspend code cannot know how to do this.

So x86-64 has this file.

===============================================================================
BIG PICTURE
===============================================================================

Suspend path:

    save_processor_state()
        |
        v
    __save_processor_state()
        |
        +--> save descriptor tables
        +--> save segment registers
        +--> save FS/GS MSRs
        +--> save control registers
        |
        v
    low-level suspend continues

Resume path:

    restore_processor_state()
        |
        v
    __restore_processor_state()
        |
        +--> restore control registers
        +--> restore GDT/IDT
        +--> restore segments
        +--> restore FS/GS bases
        +--> fix_processor_context()
        +--> restore FPU ownership
        +--> restore MTRRs

===============================================================================
KEY STRUCTURE
===============================================================================

struct saved_context saved_context;

This is the saved CPU state.

It stores:

    descriptor table info

    segment selectors

    control registers

    FS/GS base MSRs

    task register

The assembly file stores general-purpose registers separately:

    saved_context_rax
    saved_context_rbx
    saved_context_rsp
    ...

===============================================================================
PART 1: SAVING PROCESSOR STATE
===============================================================================

Function:

    save_processor_state()

calls:

    __save_processor_state(&saved_context)

===============================================================================
__save_processor_state()
===============================================================================

Purpose:

    save CPU state that must survive suspend/resume.

First:

    kernel_fpu_begin()

Why?

    It saves/restores FPU ownership state so kernel can safely touch
    FPU/SSE-related state during suspend.

===============================================================================
SAVE DESCRIPTOR TABLES
===============================================================================

Instructions:

    sgdt
    sidt
    str

Meaning:

    sgdt = store GDT descriptor

    sidt = store IDT descriptor

    str  = store task register

Saved into:

    ctxt->gdt_limit
    ctxt->idt_limit
    ctxt->tr

===============================================================================
WHAT IS GDT?
===============================================================================

GDT = Global Descriptor Table.

Even in 64-bit mode, Linux still needs GDT for:

    kernel code segment
    user code segment
    data segments
    TSS descriptor
    TLS descriptors
    compatibility mode

===============================================================================
WHAT IS IDT?
===============================================================================

IDT = Interrupt Descriptor Table.

It contains handlers for:

    exceptions
    interrupts
    system traps

Without a valid IDT:

    interrupts/exceptions cannot be handled.

===============================================================================
WHAT IS TR?
===============================================================================

TR = Task Register.

It points to the current CPU's TSS.

In x86-64 Linux, TSS is not used for hardware task switching.

It is used for:

    kernel stack on privilege transition
    IST exception stacks
    I/O permission bitmap

===============================================================================
SAVE SEGMENT REGISTERS
===============================================================================

Saved:

    DS
    ES
    FS
    GS
    SS

Even in 64-bit mode, FS/GS are still very important.

FS often points to user TLS.

GS is used for per-CPU/kernel data.

===============================================================================
SAVE FS/GS BASE MSRs
===============================================================================

Read:

    MSR_FS_BASE
    MSR_GS_BASE
    MSR_KERNEL_GS_BASE

These contain 64-bit bases.

Important:

    selector value alone is not enough on x86-64.

The base address lives in MSRs.

===============================================================================
FS/GS MODEL
===============================================================================

FS selector:

    small visible selector value

FS base MSR:

    actual 64-bit base address

Example:

    FS_BASE -> userspace TLS

GS_BASE -> kernel PDA/per-CPU data

KERNEL_GS_BASE -> swapped GS base for syscall/sysret path

===============================================================================
SAVE CONTROL REGISTERS
===============================================================================

Saved:

    CR0
    CR2
    CR3
    CR4
    CR8

Meaning:

CR0:
    protected mode, paging, write-protect, FPU bits

CR2:
    last page fault address

CR3:
    page table root

CR4:
    PAE, PGE, MCE, PSE, etc.

CR8:
    task priority register / interrupt priority on x86-64

===============================================================================
PART 2: RESTORING PROCESSOR STATE
===============================================================================

Function:

    restore_processor_state()

calls:

    __restore_processor_state(&saved_context)

===============================================================================
RESTORE ORDER MATTERS
===============================================================================

The function restores:

    CR8
    CR4
    CR3
    CR2
    CR0

Then:

    GDT
    IDT

Then:

    segment registers

Then:

    FS/GS base MSRs

Then:

    fix_processor_context()

Then:

    FPU and MTRRs

===============================================================================
WHY RESTORE CONTROL REGISTERS FIRST?
===============================================================================

Control registers define the fundamental execution environment:

    paging
    page table root
    CPU mode features
    interrupt priority

The rest of CPU state depends on these being sane.

===============================================================================
RESTORE GDT/IDT
===============================================================================

Instructions:

    lgdt
    lidt

After resume, CPU now has the correct descriptor tables again.

===============================================================================
RESTORE SEGMENTS
===============================================================================

Restores:

    DS
    ES
    FS
    GS
    SS

GS uses:

    load_gs_index()

because GS is special on x86-64.

===============================================================================
RESTORE FS/GS BASES
===============================================================================

Writes:

    MSR_FS_BASE
    MSR_GS_BASE
    MSR_KERNEL_GS_BASE

This reestablishes TLS/per-CPU base addresses.

===============================================================================
PART 3: fix_processor_context()
===============================================================================

This function repairs state that cannot be restored by simple register loads.

It handles:

    TSS descriptor
    syscall MSRs
    TR
    LDT
    debug registers

===============================================================================
TSS FIXUP
===============================================================================

Code:

    set_tss_desc(cpu, t)

    cpu_gdt(cpu)[GDT_ENTRY_TSS].type = 9

Why?

Old x86 hardware tracks TSS busy state.

Even though Linux does not use hardware task switching, the CPU still has
rules about TSS descriptors.

So the descriptor is rewritten and marked available.

===============================================================================
REINITIALIZE SYSCALL MSRs
===============================================================================

Code:

    syscall_init()

This writes:

    MSR_STAR
    MSR_LSTAR
    MSR_SYSCALL_MASK

Without this:

    syscall instruction may jump to wrong place or use wrong segments.

===============================================================================
RELOAD TR
===============================================================================

Code:

    load_TR_desc()

This executes:

    ltr

and reloads the task register.

Needed after TSS descriptor repair.

===============================================================================
RELOAD LDT
===============================================================================

Code:

    load_LDT(&current->active_mm->context)

Restores current process LDT.

Usually empty in normal 64-bit processes, but required for compatibility.

===============================================================================
RESTORE DEBUG REGISTERS
===============================================================================

If current task has hardware breakpoints:

    current->thread.debugreg7 != 0

then reload:

    DR0
    DR1
    DR2
    DR3
    DR6
    DR7

This restores debugger/hardware watchpoint state.

===============================================================================
FPU END
===============================================================================

Function:

    do_fpu_end()

calls:

    kernel_fpu_end()

This returns FPU ownership back to normal.

===============================================================================
MTRR RESTORE
===============================================================================

Code:

    mtrr_ap_init()

MTRR = Memory Type Range Register.

Controls cache type for physical address ranges:

    write-back
    uncached
    write-combining

After suspend/resume, MTRRs may need to be restored.

===============================================================================
PART 4: SOFTWARE SUSPEND TEMPORARY MAPPINGS
===============================================================================

Under:

    CONFIG_SOFTWARE_SUSPEND

this file supports hibernation image restore.

Important external assembly function:

    restore_image()

defined in suspend_asm.S

===============================================================================
WHY TEMPORARY MAPPINGS ARE NEEDED
===============================================================================

During hibernation resume:

    the fresh boot kernel copies old saved kernel pages back
    over the currently running memory

Normal page tables may be overwritten or invalidated.

So the kernel builds temporary safe page tables:

    temp_level4_pgt

Then assembly switches to them before copying pages.

===============================================================================
set_up_temporary_mappings()
===============================================================================

Purpose:

    build temporary page tables for image restore.

Flow:

    allocate new top-level PML4

    reuse original kernel mapping

    build direct mapping from scratch

===============================================================================
RESUING KERNEL MAPPING
===============================================================================

Code:

    set_pgd(temp_level4_pgt + pgd_index(__START_KERNEL_map),
            init_level4_pgt[pgd_index(__START_KERNEL_map)]);

Meaning:

    keep kernel text/data mapping available.

===============================================================================
BUILD DIRECT MAPPING
===============================================================================

Loop from:

    pfn_to_kaddr(0)

to:

    pfn_to_kaddr(end_pfn)

For each PGD range:

    allocate PUD

    fill PMDs

Uses large pages:

    _PAGE_PSE

This maps physical RAM directly.

===============================================================================
res_phys_pud_init()
===============================================================================

Builds PUD/PMD entries for physical memory.

For each PMD-sized chunk:

    set_pmd(pmd, physical_address | flags)

Flags include:

    present
    writable
    kernel
    large page
    NX if supported

===============================================================================
WHY LARGE PAGES?
===============================================================================

PMD large pages are simpler and faster.

Instead of building millions of PTEs:

    one PMD maps a large region.

This is perfect for temporary restore mappings.

===============================================================================
swsusp_arch_resume()
===============================================================================

Architecture hibernation resume entry.

Flow:

    set_up_temporary_mappings()

    restore_image()

After restore_image(), control returns into the restored old kernel.

===============================================================================
IMPORTANT COMMENT
===============================================================================

Code says:

    We have got enough memory and from now on we cannot recover

Meaning:

Once restore_image() begins:

    the current kernel is overwritten

If restore fails after that, there is no safe rollback.

===============================================================================
COMPLETE SUSPEND SAVE FLOW
===============================================================================

suspend requested
      |
      v
save_processor_state()
      |
      v
save GDT/IDT/TR
save segment registers
save FS/GS bases
save control registers
      |
      v
swsusp_arch_suspend()
      |
      v
save general registers
      |
      v
swsusp_save()
      |
      v
hibernate image written

===============================================================================
COMPLETE HIBERNATION RESUME FLOW
===============================================================================

fresh kernel boots
      |
      v
reads saved image
      |
      v
swsusp_arch_resume()
      |
      v
set_up_temporary_mappings()
      |
      v
restore_image()
      |
      v
copy saved pages over current memory
      |
      v
restore general registers
      |
      v
return into old kernel
      |
      v
restore_processor_state()
      |
      v
restore GDT/IDT/TR/segments/CRs/MSRs/FPU/MTRRs

===============================================================================
RELATION TO suspend_asm.S
===============================================================================

suspend.c:

    saves/restores structured CPU state

    builds temporary page tables

------------------------------------------------------------

suspend_asm.S:

    saves/restores general registers

    copies memory image

    switches CR3 during restore

Together:

    complete x86-64 hibernation resume mechanism.

===============================================================================
RELATION TO setup64.c
===============================================================================

setup64.c initializes CPU state at boot.

suspend.c restores CPU state after suspend.

They touch many of the same things:

    GDT
    IDT
    TSS
    syscall MSRs
    debug registers
    FS/GS bases

===============================================================================
MENTAL MODEL
===============================================================================

setup64.c:

    initialize CPU for first time

suspend.c:

    save CPU state before sleep

    restore CPU state after wake

suspend_asm.S:

    restore the entire memory image safely

Think of suspend.c as:

    CPU checkpoint/restore code

and suspend_asm.S as:

    memory image replacement code

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/suspend.c saves and restores the non-general-register
x86-64 CPU state needed across suspend and hibernation, including descriptor
tables, segment registers, FS/GS base MSRs, control registers, TSS/LDT/syscall
context, debug registers, FPU ownership, and MTRRs, and also builds temporary
page tables used by suspend_asm.S to safely restore the hibernated memory image.
===============================================================================
```

