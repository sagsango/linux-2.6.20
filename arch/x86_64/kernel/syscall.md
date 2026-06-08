```text
===============================================================================
FILE: arch/x86_64/kernel/syscall_table.c
PURPOSE: x86-64 SYSTEM CALL DISPATCH TABLE
KERNEL: Linux 2.6.x
===============================================================================

BACKGROUND
===============================================================================

A system call is how userspace asks the kernel to do privileged work.

Examples:

    read()
    write()
    open()
    mmap()
    fork()
    execve()
    exit()

Userspace cannot directly access kernel internals.

So it executes a syscall instruction.

===============================================================================
BIG PICTURE
===============================================================================

Userspace
    |
    v
syscall instruction
    |
    v
CPU jumps to kernel syscall entry
    |
    v
kernel reads syscall number from RAX
    |
    v
sys_call_table[RAX]
    |
    v
actual sys_* function

===============================================================================
WHAT THIS FILE DOES
===============================================================================

This file builds:

    sys_call_table[]

That table maps:

    syscall number -> syscall handler function

Example conceptually:

    0  -> sys_read
    1  -> sys_write
    2  -> sys_open
    3  -> sys_close
    ...
    N  -> sys_ni_syscall

===============================================================================
IMPORTANT TYPE
===============================================================================

typedef void (*sys_call_ptr_t)(void);

Meaning:

    sys_call_ptr_t is a pointer to a syscall function.

The table is:

    const sys_call_ptr_t sys_call_table[]

So each entry is a function pointer.

===============================================================================
WHY void (*)(void)?
===============================================================================

Syscalls have different C prototypes.

Example:

    sys_read(fd, buf, count)

    sys_mmap(addr, len, prot, flags, fd, off)

    sys_exit(status)

But the low-level syscall entry code passes arguments using registers.

So the table stores generic function pointers.

The assembly entry path calls the selected function according to ABI.

===============================================================================
SOURCE OF SYSCALL NUMBERS
===============================================================================

This file includes:

    <asm-x86_64/unistd.h>

That header defines syscall numbers.

Example conceptually:

    #define __NR_read 0
    #define __NR_write 1

and macro entries like:

    __SYSCALL(__NR_read, sys_read)
    __SYSCALL(__NR_write, sys_write)

===============================================================================
TWO-PASS MACRO TRICK
===============================================================================

This file includes unistd.h twice with different definitions of:

    __SYSCALL

This is a common kernel macro technique.

===============================================================================
PASS 1: DECLARE FUNCTIONS
===============================================================================

First definition:

    #define __SYSCALL(nr, sym) extern asmlinkage void sym(void);

Then include:

    <asm-x86_64/unistd.h>

Result:

    extern asmlinkage void sys_read(void);
    extern asmlinkage void sys_write(void);
    extern asmlinkage void sys_open(void);

Purpose:

    tell compiler these syscall symbols exist.

===============================================================================
WHY extern DECLARATIONS ARE NEEDED
===============================================================================

Later the table stores:

    sys_read
    sys_write

The compiler must know these are function symbols.

So pass 1 creates declarations.

===============================================================================
PASS 2: BUILD TABLE ENTRIES
===============================================================================

Second definition:

    #define __SYSCALL(nr, sym) [ nr ] = sym,

Then include:

    <asm-x86_64/unistd.h>

Result:

    [__NR_read]  = sys_read,
    [__NR_write] = sys_write,
    [__NR_open]  = sys_open,

This initializes the syscall table.

===============================================================================
DEFAULT ENTRY: sys_ni_syscall
===============================================================================

The table starts with:

    [0 ... __NR_syscall_max] = &sys_ni_syscall

Meaning:

    initialize every entry to "not implemented"

Then the included syscall list overwrites valid entries.

===============================================================================
WHAT IS sys_ni_syscall?
===============================================================================

sys_ni_syscall means:

    not implemented syscall

If userspace calls an unused syscall number:

    kernel returns -ENOSYS

Example:

    syscall(999999)

returns:

    Function not implemented

===============================================================================
WHY DEFAULT ALL ENTRIES?
===============================================================================

Safety.

If a syscall number is missing:

    it points to sys_ni_syscall

instead of garbage memory.

===============================================================================
DESIGNATED INITIALIZERS
===============================================================================

Syntax:

    [ nr ] = sym

Example:

    [0] = sys_read,
    [1] = sys_write,

This lets syscall numbers be sparse.

The table does not depend on ordering in the source file.

===============================================================================
SYSTEM CALL DISPATCH FLOW
===============================================================================

User code:

    write(1, "hi", 2)

glibc:

    RAX = __NR_write
    RDI = 1
    RSI = pointer
    RDX = 2
    syscall

CPU:

    jumps to kernel entry point from MSR_LSTAR

Kernel entry:

    reads RAX

    indexes sys_call_table[RAX]

    calls sys_write

===============================================================================
ASCII FLOW
===============================================================================

RAX = 1
 |
 v
sys_call_table[1]
 |
 v
sys_write()

===============================================================================
RELATION TO setup64.c
===============================================================================

setup64.c sets:

    MSR_LSTAR = system_call

That tells CPU where to enter kernel on syscall.

syscall_table.c provides:

    syscall number -> function

So:

    setup64.c decides entry address

    syscall_table.c decides final handler

===============================================================================
RELATION TO entry.S
===============================================================================

entry.S contains syscall entry assembly.

It does roughly:

    save registers

    validate syscall number

    call sys_call_table[rax]

    restore registers

    return to userspace

This file only provides the table.

===============================================================================
RELATION TO sys_x86_64.c
===============================================================================

sys_x86_64.c defines some syscall implementations/wrappers:

    sys_pipe
    sys_mmap
    sys_uname

syscall_table.c maps syscall numbers to those functions.

===============================================================================
WHY __NO_STUBS?
===============================================================================

The file defines:

    __NO_STUBS

before including unistd.h.

This controls how unistd.h expands syscall declarations.

It prevents generating unwanted syscall stubs in this context.

Here we only want declarations/table entries.

===============================================================================
WHY _ASM_X86_64_UNISTD_H_ IS UNDEFINED
===============================================================================

Header guards normally prevent including a header twice.

But this file intentionally includes:

    unistd.h

twice with different macro definitions.

So it does:

    #undef _ASM_X86_64_UNISTD_H_

before each include.

That forces the header contents to be processed again.

===============================================================================
COMPLETE BUILD-TIME FLOW
===============================================================================

Compiler sees syscall_table.c

    |
    v

define __SYSCALL as extern declaration

    |
    v

include unistd.h

    |
    v

generate syscall externs

    |
    v

redefine __SYSCALL as table initializer

    |
    v

include unistd.h again

    |
    v

generate sys_call_table[]

===============================================================================
RUNTIME FLOW
===============================================================================

Userspace syscall

    |
    v

RAX contains syscall number

    |
    v

system_call entry code

    |
    v

sys_call_table[RAX]

    |
    v

sys_* function

===============================================================================
MENTAL MODEL
===============================================================================

Think of this file as a switch statement built at compile time.

Instead of:

    switch (nr) {
        case 0: return sys_read();
        case 1: return sys_write();
    }

Linux uses:

    sys_call_table[nr]()

This is faster and cleaner.

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

syscall_table.c constructs the x86-64 syscall dispatch table by using
the __SYSCALL macro list from unistd.h twice: first to declare syscall
handler symbols, then to initialize sys_call_table[] entries, with all
unassigned syscall numbers safely defaulting to sys_ni_syscall.
===============================================================================
```

