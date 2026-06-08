```text
===============================================================================
FILE: arch/x86_64/kernel/x8664_ksyms.c
PURPOSE: EXPORT LOW-LEVEL x86-64 KERNEL SYMBOLS TO LOADABLE MODULES
KERNEL: Linux 2.6.x x86-64
===============================================================================

BACKGROUND
===============================================================================

Linux supports:

    1. Built-in kernel code
    2. Loadable kernel modules (.ko)

Examples:

    e1000.ko
    ext4.ko
    nfs.ko
    kvm.ko

Modules are compiled separately from the kernel image.

When a module loads:

    insmod mydriver.ko

the module may reference functions located inside the kernel:

    memcpy()
    copy_to_user()
    kernel_thread()
    load_gs_index()

The module does not contain those implementations.

It only contains references.

The kernel loader must resolve them.

===============================================================================
HOW MODULE SYMBOL RESOLUTION WORKS
===============================================================================

Kernel image:

    vmlinux

contains:

    memcpy()
    copy_to_user()
    clear_page()
    ...

Module:

    mydriver.ko

contains:

    undefined symbol memcpy

Loader:

    find symbol
    patch relocation
    module starts

Exactly like dynamic linking in userspace.

===============================================================================
EXPORT_SYMBOL()
===============================================================================

A kernel symbol is NOT automatically visible to modules.

It must be explicitly exported.

Example:

    EXPORT_SYMBOL(memcpy);

This places metadata into:

    __ksymtab

during linking.

Later:

    insmod

can find it.

===============================================================================
WITHOUT EXPORT_SYMBOL
===============================================================================

Module:

    extern void copy_page(void);

    copy_page(...);

Loading:

    insmod mymodule.ko

fails:

    unresolved symbol copy_page

because symbol was not exported.

===============================================================================
WHAT THIS FILE DOES
===============================================================================

This file exports symbols implemented in:

    assembly files
    architecture-specific code

Most normal C exports are done near their implementations.

This file is special.

Comment says:

    Exports for assembly files.

Meaning:

    code exists elsewhere
    export list lives here

===============================================================================
HIGH LEVEL CONTENTS
===============================================================================

Exports:

    kernel thread support
    semaphore slow paths
    user-copy helpers
    page operations
    rwlock slow paths
    memory routines
    page tables
    GS helpers

===============================================================================
WHY MANY OF THESE ARE ASSEMBLY ROUTINES
===============================================================================

Many x86 operations need:

    special instructions
    exception fixups
    segment manipulation
    optimized copies

Examples:

    copy_to_user()
    copy_from_user()
    clear_page()
    memcpy()

Historically implemented in assembly.

===============================================================================
KERNEL THREAD EXPORT
===============================================================================

EXPORT_SYMBOL(kernel_thread);

===============================================================================

kernel_thread()

creates a new kernel thread.

Used before kthread API became common.

Example:

    kernel_thread(worker, NULL, CLONE_FS);

Flow:

    module
       |
       v
    kernel_thread()
       |
       v
    copy task
       |
       v
    scheduler
       |
       v
    new kernel thread

===============================================================================
SEMAPHORE SLOW PATHS
===============================================================================

EXPORT_SYMBOL(__down_failed);
EXPORT_SYMBOL(__down_failed_interruptible);
EXPORT_SYMBOL(__down_failed_trylock);
EXPORT_SYMBOL(__up_wakeup);

===============================================================================

BACKGROUND
===============================================================================

Semaphore fast path:

    count > 0

simply decrements count.

No sleeping.

Example:

    down(&sem)

Fast path:

    sem->count = 3

becomes:

    sem->count = 2

Done.

===============================================================================
WHEN FAST PATH FAILS
===============================================================================

count == 0

Need to sleep.

Flow:

    down()
      |
      v
    atomic decrement failed
      |
      v
    __down_failed()
      |
      v
    enqueue waiter
      |
      v
    schedule()

===============================================================================

__down_failed()
    sleep until available

__down_failed_interruptible()
    sleep but allow signals

__down_failed_trylock()
    special trylock path

__up_wakeup()
    wake sleeping task

===============================================================================
ASCII FLOW
===============================================================================

down()
  |
  +--> count > 0 ?
  |       |
  |       +--> success
  |
  +--> no
          |
          v
    __down_failed()
          |
          v
       sleep

up()
  |
  v
__up_wakeup()
  |
  v
wake task

===============================================================================
USER ACCESS HELPERS
===============================================================================

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);
EXPORT_SYMBOL(__get_user_8);

EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);

===============================================================================

BACKGROUND
===============================================================================

Kernel cannot trust user pointers.

User may pass:

    NULL

or

    unmapped address

or

    malicious pointer

Therefore:

    *ptr

inside kernel is unsafe.

Instead:

    get_user()
    put_user()

===============================================================================
EXAMPLE
===============================================================================

User:

    read(fd, buf, 4096)

Kernel receives:

    buf = user pointer

Copy:

    copy_to_user(buf,...)

internally may call:

    __put_user_1
    __put_user_2
    __put_user_4
    __put_user_8

depending on size.

===============================================================================
WHY DIFFERENT SIZES?
===============================================================================

Different instructions.

1-byte:

    movb

2-byte:

    movw

4-byte:

    movl

8-byte:

    movq

Optimized separately.

===============================================================================
COPY HELPERS
===============================================================================

EXPORT_SYMBOL(copy_user_generic);
EXPORT_SYMBOL(copy_from_user);
EXPORT_SYMBOL(copy_to_user);
EXPORT_SYMBOL(__copy_from_user_inatomic);

===============================================================================

These are heavily used.

Example:

    read()
    write()
    ioctl()
    mmap()
    networking

===============================================================================
NORMAL COPY
===============================================================================

copy_from_user()

User memory
     |
     v
 Kernel buffer

Handles faults safely.

===============================================================================
ATOMIC COPY
===============================================================================

__copy_from_user_inatomic()

Used when sleeping is forbidden.

Example:

    spinlock held
    interrupt context

Cannot fault and sleep.

Special version required.

===============================================================================
PAGE OPERATIONS
===============================================================================

EXPORT_SYMBOL(copy_page);
EXPORT_SYMBOL(clear_page);

===============================================================================

copy_page()

copies 4KB page.

Equivalent:

    memcpy(dst, src, PAGE_SIZE)

but highly optimized.

Often uses:

    rep movsq

or other CPU-specific tricks.

===============================================================================

clear_page()

fills page with zeros.

Equivalent:

    memset(page,0,PAGE_SIZE)

but optimized.

===============================================================================
MEMORY MANAGEMENT USES
===============================================================================

fork()
page migration
copy-on-write
page allocator
swap

all use:

    copy_page()
    clear_page()

===============================================================================
SMP RWLOCK SLOW PATHS
===============================================================================

CONFIG_SMP only:

    __write_lock_failed()
    __read_lock_failed()

===============================================================================

BACKGROUND
===============================================================================

rwlock fast path:

    lock available

acquire immediately.

===============================================================================
CONTENDED CASE
===============================================================================

CPU0:
    write_lock()

CPU1:
    write_lock()

CPU1 fails.

Falls into:

    __write_lock_failed()

which spins until lock available.

Same for readers.

===============================================================================
ASCII FLOW
===============================================================================

write_lock()
    |
    +--> fast success
    |
    +--> contention
            |
            v
      __write_lock_failed()

===============================================================================
MEMORY FUNCTIONS
===============================================================================

#undef memcpy
#undef memset
#undef memmove

EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(__memcpy);

===============================================================================

WHY EXPORT THESE?
===============================================================================

Normally GCC generates:

    inline copies

using builtins.

But not always.

Sometimes compiler emits:

    call memcpy

inside module.

Module then requires exported symbol.

Without export:

    unresolved symbol memcpy

===============================================================================
SPECIAL NOTE
===============================================================================

Comment explains:

    gcc sometimes decides not to inline them

Thus export is necessary.

===============================================================================
ZERO PAGE
===============================================================================

EXPORT_SYMBOL(empty_zero_page);

===============================================================================

WHAT IS IT?
===============================================================================

One permanently allocated physical page:

    filled with zeros

Shared everywhere.

===============================================================================
EXAMPLE
===============================================================================

Anonymous memory:

    mmap(...)

Before first write:

    many mappings point to same zero page

Memory saving.

===============================================================================
ASCII
===============================================================================

Process A
     |
     +--> empty_zero_page

Process B
     |
     +--> empty_zero_page

Process C
     |
     +--> empty_zero_page

All read same page.

Write causes:

    COW

===============================================================================
INITIAL PAGE TABLE
===============================================================================

EXPORT_SYMBOL(init_level4_pgt);

===============================================================================

This is the kernel's top-level page table.

On x86-64:

    CR3
      |
      v
   PML4

Linux 2.6 called it:

    level4 page table

Modules occasionally need access.

===============================================================================
PAGE TABLE HIERARCHY
===============================================================================

CR3
 |
 v
PGD (init_level4_pgt)
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

===============================================================================
GS HELPER
===============================================================================

EXPORT_SYMBOL(load_gs_index);

===============================================================================

BACKGROUND
===============================================================================

x86-64 heavily uses:

    FS
    GS

for per-thread and per-cpu data.

Kernel often keeps:

    current task
    PDA
    per-cpu variables

through GS.

===============================================================================

load_gs_index()

loads a GS selector.

Used during:

    context switching
    compatibility code
    TLS setup

===============================================================================
HOW MODULE LOADING USES THESE EXPORTS
===============================================================================

mymodule.ko

contains:

    call copy_to_user
    call clear_page

Module loader:

    parse ELF relocations
          |
          v
    lookup __ksymtab
          |
          v
    find copy_to_user address
          |
          v
    patch relocation
          |
          v
    module ready

===============================================================================
RELATION TO module.c
===============================================================================

Earlier x86_64 module loader code you studied:

    apply_relocate_add()

processes relocations.

That code resolves references to:

    memcpy
    copy_to_user
    clear_page
    kernel_thread

using exported symbols from files like this one.

===============================================================================
MENTAL MODEL
===============================================================================

Think of this file as:

    "The public API list for low-level x86-64 assembly helpers."

The actual code lives elsewhere.

This file simply says:

    "Modules are allowed to use these symbols."

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/x8664_ksyms.c exports critical low-level x86-64 kernel
functions and data structures—including semaphore slow paths, user-copy
routines, page operations, memory functions, page tables, GS helpers, and
kernel thread support—so that loadable kernel modules can resolve and use
them through the kernel symbol table during module loading.
===============================================================================
```

