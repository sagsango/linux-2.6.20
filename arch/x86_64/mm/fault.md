Here's an IDE-style note for **Linux 2.6 x86-64 Page Fault Handling (`fault.c`)**, written as a single study file.

```text
================================================================================
Linux 2.6 x86-64 Page Fault Handling
File: arch/x86_64/mm/fault.c
================================================================================

BACKGROUND
================================================================================

Every memory access performed by the CPU goes through the MMU.

Example:

    mov (%rax), %rbx

CPU translates:

    Virtual Address
           |
           v
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
    Physical Page

If translation fails:

    - page not present
    - permission violation
    - reserved bit corruption
    - NX violation

CPU generates:

    #PF (Page Fault)
    Vector = 14

Hardware automatically saves:

    RIP
    RSP
    RFLAGS
    CS
    SS

and writes faulting address into:

    CR2

then jumps through:

    IDT[14]

which eventually reaches:

    do_page_fault()

This file is the central dispatcher for ALL memory faults.

================================================================================
BIG PICTURE
================================================================================

               User Process
                     |
                     v
              Memory Access
                     |
                     v
              MMU Translation
                     |
             +-------+-------+
             |               |
             | Success       | Fail
             |               |
             v               v
          Continue         #PF
                              |
                              v
                     do_page_fault()
                              |
      +-----------------------+-----------------------+
      |                                               |
      v                                               v
 User Address                                   Kernel Address
      |                                               |
      v                                               v
 find_vma()                              vmalloc_fault()
      |                                 extable lookup
      |                                 OOPS handling
      |
      v
 handle_mm_fault()
      |
      v
 retry instruction

================================================================================
CPU ERROR CODE
================================================================================

#define PF_PROT   (1<<0)
#define PF_WRITE  (1<<1)
#define PF_USER   (1<<2)
#define PF_RSVD   (1<<3)
#define PF_INSTR  (1<<4)

Bit Meaning

0   Protection violation
1   Write access
2   User mode fault
3   Reserved bit violation
4   Instruction fetch fault

Examples:

00000 = page not present

00010 = write to missing page

00100 = user read missing page

00111 = user write protection violation

10000 = instruction fetch fault

================================================================================
ENTRY POINT
================================================================================

asmlinkage void do_page_fault(...)

First thing:

    mov CR2 -> address

Faulting address:

    address = CR2

Current task:

    current

Current address space:

    current->mm

================================================================================
STEP 1 : KERNEL ADDRESS?
================================================================================

if (address >= TASK_SIZE64)

Kernel-space fault.

Examples:

    vmalloc
    module memory
    kernel text
    kernel data

Flow:

        Kernel Fault
              |
              v
      vmalloc_fault()
              |
      extable lookup
              |
              v
            OOPS

================================================================================
STEP 2 : USER ADDRESS?
================================================================================

Normal userspace page fault.

Examples:

    malloc()
    stack growth
    mmap()
    COW
    swap-in

Flow:

    find_vma(mm,address)

================================================================================
find_vma()
================================================================================

Searches:

    mm->mmap

for VMA containing address.

Example:

    0x400000 - 0x500000  text

    0x600000 - 0x700000  heap

    0x7fff0000           stack

Case A:

    address inside VMA

        GOOD

Case B:

    below stack

        maybe expand stack

Case C:

    no VMA

        SIGSEGV

================================================================================
STACK GROWTH
================================================================================

if (VM_GROWSDOWN)

Example:

    push %rax

touches:

    rsp - 8

below current stack mapping.

Kernel executes:

    expand_stack()

creates new pages.

================================================================================
ACCESS PERMISSION CHECK
================================================================================

switch(error_code)

Checks:

    VM_READ
    VM_WRITE
    VM_EXEC

Examples:

Write fault:

    VM_WRITE required

Read fault:

    VM_READ required

Otherwise:

    SIGSEGV

================================================================================
handle_mm_fault()
================================================================================

Most important MM function.

Possible operations:

    Allocate anonymous page

    Read page from disk

    Swap page in

    Copy-On-Write

    Populate mmap page

Flow:

          handle_mm_fault()
                    |
      +-------------+-------------+
      |             |             |
      v             v             v
    Minor         Major        Error
    Fault         Fault

================================================================================
MINOR FAULT
================================================================================

Page already exists.

Need only page-table update.

Example:

    Shared page already in RAM

Counters:

    current->min_flt++

================================================================================
MAJOR FAULT
================================================================================

Requires IO.

Example:

    Read executable page

        from disk

Counters:

    current->maj_flt++

================================================================================
BAD USER ACCESS
================================================================================

Example:

    *(int *)0xdeadbeef

No VMA found.

Kernel generates:

    SIGSEGV

Flow:

        bad_area()
              |
              v
       force_sig_info()
              |
              v
          SIGSEGV

================================================================================
KERNEL PAGE FAULTS
================================================================================

Much more dangerous.

Example:

    kernel dereference NULL

        *ptr

Flow:

    no_context()

================================================================================
EXCEPTION TABLE FIXUPS
================================================================================

Before crashing:

    search_exception_tables(RIP)

Used by:

    copy_from_user()
    copy_to_user()
    get_user()
    put_user()

Flow:

       Fault
         |
         v
 search_exception_tables()
         |
   +-----+------+
   |            |
 found       not found
   |            |
   v            v
 recover      OOPS

================================================================================
EXAMPLE
================================================================================

copy_from_user()

    mov (%userptr), %rax

User pointer invalid.

CPU:

    #PF

Kernel:

    do_page_fault()

finds extable entry.

Instead of crashing:

    returns -EFAULT

================================================================================
VMALLOC FAULTS
================================================================================

vmalloc mappings live in kernel page tables.

Sometimes current process page tables are missing them.

vmalloc_fault()

copies:

    init_mm

entries into:

    current->mm

and retries.

================================================================================
PREFETCH BUG WORKAROUND
================================================================================

Function:

    is_prefetch()

Old AMD K8 CPUs occasionally generated fake page faults for:

    prefetch

instructions.

Kernel detects and ignores.

================================================================================
ERRATA 93 WORKAROUND
================================================================================

Function:

    is_errata93()

Old BIOS/SMM bug:

    upper RIP bits corrupted

Kernel reconstructs RIP and continues.

================================================================================
KERNEL OOPS PATH
================================================================================

If recovery impossible:

        no_context()
              |
              v
           __die()
              |
              v
        dump registers
        dump pagetable
        dump RIP
              |
              v
          do_exit()

Output:

    Unable to handle kernel paging request

================================================================================
PAGE TABLE DUMP
================================================================================

dump_pagetable(address)

Prints:

    PGD
    PUD
    PMD
    PTE

Useful for MM debugging.

================================================================================
VMALLOC SYNCHRONIZATION
================================================================================

vmalloc_sync_all()

Synchronizes kernel vmalloc mappings into every process page table.

Needed because:

    kernel mappings shared
    userspace mappings private

================================================================================
COMPLETE PAGE FAULT LIFECYCLE
================================================================================

User instruction

        mov (%rax), %rbx

                |
                v

             MMU

                |
                v

        Translation fails

                |
                v

              #PF

                |
                v

          CR2 = address

                |
                v

        do_page_fault()

                |
        +-------+--------+
        |                |
        v                v

     User          Kernel

        |                |
        v                v

    find_vma()      extable
        |           vmalloc
        |           OOPS

        v

 handle_mm_fault()

        |
        v

 page allocated
 page loaded
 page swapped
 COW handled

        |
        v

 return from exception

        |
        v

 CPU retries instruction

        |
        v

 Success

================================================================================
FILES CLOSELY RELATED
================================================================================

trap.c
    installs #PF handler

entry.S
    low-level exception entry

extable.c
    exception table search

memory.c
    handle_mm_fault()

mmap.c
    VMA management

pgtable.h
    page table helpers

================================================================================
KEY TAKEAWAY
================================================================================

CPU detects translation problem
        |
        v
#PF (vector 14)
        |
        v
do_page_fault()
        |
        +--> find_vma()
        +--> handle_mm_fault()
        +--> stack expansion
        +--> swap-in
        +--> COW
        +--> SIGSEGV
        +--> exception-table recovery
        +--> kernel OOPS

This file is the central traffic controller for the entire Linux virtual
memory subsystem.
================================================================================
```

