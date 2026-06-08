```text
============================================================
KEXEC RELOCATION STUB
File: arch/x86_64/kernel/relocate_kernel.S
Linux 2.6.x Era
============================================================

PURPOSE
------------------------------------------------------------

This file is the tiny assembly stub that performs the final
"kexec jump" from the old kernel into the new kernel.

It runs after:

    machine_kexec()

has already decided:

    "We are past the point of no return."

This code:

    - builds minimal mappings for itself
    - switches page tables
    - copies new kernel pages into final locations
    - clears CPU state
    - jumps to new kernel entry point

============================================================
BIG PICTURE
============================================================

Normal reboot:

Old Linux
   |
   v
BIOS/UEFI
   |
   v
Bootloader
   |
   v
New Linux

------------------------------------------------------------

kexec reboot:

Old Linux
   |
   v
relocate_kernel.S
   |
   v
New Linux

No BIOS.
No UEFI.
No GRUB.

============================================================
CALLING CONVENTION
============================================================

relocate_kernel(indirection_page, page_list, start)

Registers:

    %rdi = indirection_page

        describes pages to copy

    %rsi = page_list

        contains addresses of control pages and page tables

    %rdx = start address

        new kernel entry point

============================================================
WHY THIS MUST BE ASSEMBLY
============================================================

At this stage:

    interrupts are disabled

    old kernel is dying

    page tables will be changed

    stack will be changed

    normal C runtime assumptions are unsafe

So the code must be:

    small
    position-independent
    relocatable
    page-aligned
    self-contained

============================================================
IMPORTANT TERMS
============================================================

control page

    temporary page containing this relocation code

------------------------------------------------------------

indirection page

    list describing source/destination pages to copy

------------------------------------------------------------

identity mapping

    virtual address == physical address

------------------------------------------------------------

page_list

    array of physical/virtual addresses needed by this stub

============================================================
WHY CONTROL PAGE IS NEEDED
============================================================

The old kernel may overwrite memory while copying the new kernel.

So the relocation code must run from a safe page:

    control page

This page is specially reserved for kexec.

============================================================
PHASE 1:
MAP CONTROL PAGE AT VIRTUAL ADDRESS
============================================================

First part maps:

    VA_CONTROL_PAGE -> PA_CONTROL_PAGE

Why?

The code is currently executing using old kernel virtual addresses.

Before switching page tables, it needs the new page table to still
map the virtual address it is currently using.

============================================================

Flow:

    calculate PGD index

    install PUD entry

    calculate PUD index

    install PMD entry

    calculate PMD index

    install PTE entry

    map VA_CONTROL_PAGE to PA_CONTROL_PAGE

============================================================
PAGE TABLE WALK
============================================================

x86-64 4-level paging:

    PGD
     |
     v
    PUD
     |
     v
    PMD
     |
     v
    PTE
     |
     v
    page

This file manually creates those entries.

============================================================
WHY MASK 0x0000ff8000000000?
============================================================

This extracts page-table index bits from the virtual address.

The code repeats:

    mask address
    shift index
    add table base
    write entry

for each page-table level.

============================================================
PAGE_ATTR
============================================================

#define PAGE_ATTR 0x63

Meaning roughly:

    present
    writable
    accessed
    dirty

So the control page is mapped writable/present.

============================================================
PHASE 2:
IDENTITY MAP CONTROL PAGE
============================================================

Second mapping:

    PA_CONTROL_PAGE -> PA_CONTROL_PAGE

Why?

After switching to identity-mapped execution, the CPU must still
be able to fetch instructions from the control page.

============================================================

Before:

    RIP uses old virtual address

After:

    RIP uses physical identity address

============================================================

So both mappings are temporarily needed:

    virtual mapping

    identity mapping

============================================================
PHASE 3:
DISABLE INTERRUPTS / CLEAN FLAGS
============================================================

Code:

    pushq $0
    popfq

This clears RFLAGS.

Effect:

    interrupts disabled

    direction flag cleared

    trap flag cleared

    other flags known

At this stage, interrupts must not occur.

There may be no valid IDT/GDT state for normal handling.

============================================================
PHASE 4:
SAVE PHYSICAL ADDRESSES BEFORE PAGE TABLE SWITCH
============================================================

Code saves:

    PA_CONTROL_PAGE -> r8

    PA_TABLE_PAGE   -> rcx

Why?

After switching page tables, old virtual mappings may disappear.

So all required physical addresses are captured before CR3 changes.

============================================================
PHASE 5:
SWITCH TO TEMPORARY PAGE TABLES
============================================================

Code:

    movq PA_PGD, %cr3

This loads new page tables.

Effect:

    old kernel page tables no longer active

    temporary kexec mappings active

============================================================
WHY CR3 MATTERS
============================================================

CR3 points to top-level page table.

Changing CR3 changes virtual-to-physical translation.

This is the real transition away from the old kernel address space.

============================================================
PHASE 6:
SET NEW STACK
============================================================

Code:

    lea 4096(%r8), %rsp

Stack becomes:

    end of physical control page

ASCII:

control page

+----------------------+
| relocation code      |
|                      |
|                      |
| temporary stack ---> |
+----------------------+

============================================================
PHASE 7:
JUMP TO IDENTITY-MAPPED CODE
============================================================

Code computes:

    physical address of identity_mapped label

Then:

    pushq %r8
    ret

This transfers execution from virtual mapping to identity mapping.

============================================================

Before:

    RIP = virtual address of relocate_kernel

After:

    RIP = physical address of identity_mapped

============================================================
PHASE 8:
SAVE NEW KERNEL ENTRY POINT
============================================================

At identity_mapped:

    pushq %rdx

The new kernel entry point is saved on stack.

At the very end:

    ret

will jump to that address.

============================================================
PHASE 9:
NORMALIZE CR0
============================================================

CR0 is set to known state:

    paging enabled

    protected mode enabled

    write protect disabled

    alignment check disabled

    task switched cleared

    no FP emulation

Important bits:

    CR0.PG = 1

    CR0.PE = 1

============================================================
WHY SET CR0?
============================================================

The new kernel expects CPU control registers in a sane state.

The old kernel might have unusual bits set.

So kexec stub sanitizes them.

============================================================
PHASE 10:
NORMALIZE CR4
============================================================

Code:

    cr4 = 1 << 5

Meaning:

    PAE enabled

Other features disabled:

    PGE

    MCE

    PSE

    debug extensions

    performance monitoring

    SSE exceptions

============================================================
WHY CR4.PAE?
============================================================

Long mode paging depends on PAE-style page tables.

Even in x86-64, CR4.PAE is required for long mode paging.

============================================================
PHASE 11:
SWITCH TO IDENTITY-MAPPED PAGE TABLE
============================================================

Code:

    movq %rcx, %cr3

Here:

    rcx = PA_TABLE_PAGE

This installs the final identity page table.

Also flushes TLB.

============================================================
WHY IDENTITY MAPPING NOW?
============================================================

The copy loop works with physical addresses.

Identity mapping makes:

    virtual address == physical address

So assembly can copy pages using physical addresses directly.

============================================================
PHASE 12:
COPY NEW KERNEL PAGES
============================================================

This is the heart of relocation.

Input:

    indirection_page

contains encoded entries.

Each entry has flag bits:

    bit 0 = destination page

    bit 1 = indirection page

    bit 2 = done

    bit 3 = source page

============================================================
COPY LIST FORMAT
============================================================

Entries look like:

    destination page
    source page
    source page
    destination page
    source page
    done

============================================================
DESTINATION ENTRY
============================================================

If bit 0 set:

    current destination = entry & PAGE_MASK

Stored in:

    rdi

============================================================
INDIRECTION ENTRY
============================================================

If bit 1 set:

    switch to another indirection page

Stored in:

    rbx

This allows chained copy lists.

============================================================
DONE ENTRY
============================================================

If bit 2 set:

    copying finished

Jump to cleanup.

============================================================
SOURCE ENTRY
============================================================

If bit 3 set:

    source = entry & PAGE_MASK

Then copy one page:

    512 qwords

Since:

    512 * 8 = 4096 bytes

Code:

    movq $512, %rcx
    rep movsq

============================================================
COPY EXAMPLE
============================================================

Indirection list:

    DEST 0x100000
    SRC  0x700000
    DEST 0x101000
    SRC  0x701000
    DONE

Result:

    copy 0x700000 -> 0x100000

    copy 0x701000 -> 0x101000

============================================================
WHY COPY IS NEEDED
============================================================

The new kernel image may have been loaded into safe temporary pages.

But it must finally live at its required physical addresses.

This stub moves pages into final locations.

============================================================
WHY NOT COPY EARLIER?
============================================================

Because copying earlier could overwrite the running kernel.

The old kernel must stay alive until the final transition.

So copying is delayed until the relocation stub is running safely.

============================================================
PHASE 13:
FLUSH TLB / SERIALIZE
============================================================

After copying:

    movq %cr3, %rax
    movq %rax, %cr3

Reloading CR3 flushes TLB.

Also acts as serializing operation.

Reason:

    copied code/data may include executable code

    avoid stale translations/instruction effects

============================================================
PHASE 14:
CLEAR GENERAL REGISTERS
============================================================

The stub zeroes almost all registers:

    rax
    rbx
    rcx
    rdx
    rsi
    rdi
    rbp
    r8-r15

It leaves:

    rsp

because stack contains the new kernel entry point.

============================================================
WHY CLEAR REGISTERS?
============================================================

Do not leak old kernel state.

Do not pass accidental garbage to new kernel.

Start new kernel with clean CPU state.

============================================================
PHASE 15:
FINAL JUMP TO NEW KERNEL
============================================================

Earlier:

    pushq %rdx

saved start address.

At the end:

    ret

pops start address into RIP.

Execution continues at:

    image->start

The new kernel begins.

============================================================
COMPLETE FLOW
============================================================

machine_kexec()
      |
      v
copy relocate_kernel to control page
      |
      v
relocate_kernel(indirection_page, page_list, start)
      |
      +--> map control page virtually
      |
      +--> identity map control page
      |
      +--> clear flags / disable interrupts
      |
      +--> switch to temporary page table
      |
      +--> setup stack on control page
      |
      +--> jump to identity-mapped code
      |
      +--> normalize CR0/CR4
      |
      +--> switch to identity page table
      |
      +--> copy new kernel pages
      |
      +--> flush TLB
      |
      +--> clear registers
      |
      v
ret to new kernel entry point

============================================================
RELATION TO machine_kexec.c
============================================================

machine_kexec.c:

    high-level C setup

    prepares page_list

    copies relocation stub

    disables GDT/IDT

    calls relocate_kernel()

------------------------------------------------------------

relocate_kernel.S:

    low-level final transition

    switches page tables

    copies image pages

    jumps to new kernel

============================================================
RELATION TO reboot.c
============================================================

reboot.c:

    resets hardware

    BIOS/UEFI may run again

------------------------------------------------------------

kexec relocation:

    does not reset hardware

    directly starts another kernel

============================================================
MENTAL MODEL
============================================================

Think of this file as:

    bootloader inside the old kernel

But unlike GRUB:

    it runs while the old kernel is dying

    it cannot rely on firmware

    it cannot allocate memory

    it cannot call normal kernel services

    it must safely overwrite memory

============================================================
ONE-LINE SUMMARY
============================================================

relocate_kernel.S is the final x86-64 kexec trampoline: it
maps its control page, switches to temporary identity mappings,
sets safe CPU control-register state, copies the new kernel
image from temporary pages to final physical addresses using an
indirection list, clears old register state, and returns directly
into the new kernel entry point.
============================================================
```

