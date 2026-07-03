===============================================================================
FILE: arch/x86_64/mm/extable.c
PURPOSE: SEARCH THE x86-64 KERNEL EXCEPTION TABLE
KERNEL: Linux 2.6.x
===============================================================================

BACKGROUND
===============================================================================

This file is small, but very important.

It supports kernel fault recovery.

Normally, if kernel code faults, that is serious.

Example:

    kernel dereferences bad pointer
        |
        v
    page fault
        |
        v
    oops / panic

But some kernel code intentionally touches userspace memory.

Examples:

    copy_from_user()
    copy_to_user()
    get_user()
    put_user()

Userspace pointers can be invalid.

So the kernel needs a way to recover instead of crashing.

That recovery mechanism is:

    exception table

===============================================================================
WHAT IS AN EXCEPTION TABLE?
===============================================================================

An exception table is a sorted table of pairs:

    faulting instruction address  ->  fixup instruction address

Conceptually:

    struct exception_table_entry {
        unsigned long insn;
        unsigned long fixup;
    };

Meaning:

    if exception happened at insn,
    continue execution at fixup.

===============================================================================
EXAMPLE
===============================================================================

Kernel tries:

    copy_from_user(kernel_buf, user_ptr, len)

If user_ptr is bad:

    CPU raises page fault.

Instead of oopsing, kernel checks:

    is faulting RIP in exception table?

If yes:

    regs->rip = fixup;

Then execution resumes at recovery code.

===============================================================================
FAULT RECOVERY FLOW
===============================================================================

kernel instruction touches user memory
        |
        v
bad user pointer
        |
        v
#PF page fault
        |
        v
page fault handler
        |
        v
search_exception_tables(regs->rip)
        |
        +--> found
        |       |
        |       v
        |   regs->rip = fixup
        |       |
        |       v
        |   return from exception
        |
        +--> not found
                |
                v
              oops

===============================================================================
WHERE THIS FILE FITS
===============================================================================

Generic code:

    search_exception_tables()

searches all exception tables:

    main kernel exception table
    module exception tables

Architecture helper:

    search_extable()

does binary search inside one table.

This file implements that architecture search.

===============================================================================
WHY BINARY SEARCH?
===============================================================================

Exception table entries are sorted by instruction address.

So lookup can be:

    O(log n)

instead of:

    O(n)

This matters because fault recovery must be quick.

===============================================================================
FUNCTION: search_extable()
===============================================================================

Prototype:

    const struct exception_table_entry *
    search_extable(const struct exception_table_entry *first,
                   const struct exception_table_entry *last,
                   unsigned long value)

Inputs:

    first

        first entry in exception table

    last

        last entry in exception table

    value

        faulting instruction pointer

Output:

    matching exception table entry

or:

    NULL

===============================================================================
VALUE PARAMETER
===============================================================================

value is usually:

    regs->rip

The instruction address where fault happened.

Example:

    fault at copy_from_user instruction

    value = RIP of faulting load/store

===============================================================================
K8 BUG WORKAROUND
===============================================================================

Code:

    if ((value >> 32) == 0)
        value |= 0xffffffffUL << 32;

Comment:

    Work around a B stepping K8 bug

Meaning:

    Some early AMD K8 CPUs could report a truncated 32-bit fault address
    instead of full canonical kernel address.

Kernel text addresses on x86-64 are high addresses:

    ffffffff80......

If upper 32 bits are zero, the workaround sign-extends it into kernel range:

    00000000xxxxxxxx

becomes:

    ffffffffxxxxxxxx

This allows exception table lookup to still work.

===============================================================================
WHY THIS MATTERS
===============================================================================

If fault address is wrong:

    exception table lookup fails

Then kernel would oops even though fixup exists.

This workaround saves recoverable faults on affected CPUs.

===============================================================================
BINARY SEARCH LOGIC
===============================================================================

Pseudo-code:

    while first <= last:

        mid = first + (last - first) / 2

        diff = mid->insn - value

        if diff == 0:
            return mid

        if diff < 0:
            first = mid + 1

        else:
            last = mid - 1

    return NULL

===============================================================================
HOW TO READ diff
===============================================================================

If:

    mid->insn == value

found exact faulting instruction.

If:

    mid->insn < value

search upper half.

If:

    mid->insn > value

search lower half.

===============================================================================
EXAMPLE TABLE
===============================================================================

Exception table:

    insn 0xffffffff80100010 -> fixup A
    insn 0xffffffff80101020 -> fixup B
    insn 0xffffffff80102030 -> fixup C
    insn 0xffffffff80103040 -> fixup D

Fault:

    value = 0xffffffff80102030

Binary search finds entry C.

Then page fault handler changes:

    RIP = fixup C

===============================================================================
RELATION TO COPY_FROM_USER
===============================================================================

copy_from_user may contain assembly like:

    1: movb (%rsi), %al
       movb %al, (%rdi)
    2:

Exception table says:

    fault at 1b -> jump to fixup

Fixup code sets return value:

    bytes not copied

and returns error.

===============================================================================
RELATION TO traps.c
===============================================================================

traps.c uses:

    search_exception_tables(regs->rip)

For kernel traps.

If fixup exists:

    regs->rip = fixup->fixup

Otherwise:

    die()

So extable.c is what lets traps.c recover from expected kernel faults.

===============================================================================
RELATION TO vmlinux.lds.S
===============================================================================

The linker script defines:

    __start___ex_table
    __stop___ex_table

and collects:

    __ex_table

This file searches that table.

The linker script places it.

===============================================================================
RELATION TO MODULES
===============================================================================

Modules also have exception tables.

Example:

    driver.ko uses copy_from_user()

The module loader keeps module extable sorted.

Generic exception lookup searches both:

    built-in kernel table

    module tables

This function can search one such table range.

===============================================================================
WHY LAST IS INCLUSIVE
===============================================================================

The loop uses:

    while (first <= last)

So last points to the final valid entry, not one past the end.

This is important.

Wrong convention would skip last entry or read out of range.

===============================================================================
WHY TABLE MUST BE SORTED
===============================================================================

Binary search only works if:

    entries are sorted by insn

Kernel build sorts exception tables during build/link processing.

If unsorted:

    lookup could fail randomly.

===============================================================================
MENTAL MODEL
===============================================================================

Exception table is like a map:

    fault address -> recovery address

search_extable() is the lookup function.

It does not handle the fault itself.

It only answers:

    "Do we know how to recover from a fault at this RIP?"

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/mm/extable.c implements the x86-64 exception-table lookup routine:
given a faulting instruction address, it applies an early AMD K8 address
workaround and performs a binary search over the sorted exception table to find
a matching recovery entry, enabling copy_to_user/copy_from_user-style kernel
faults to resume at safe fixup code instead of causing an oops.
===============================================================================
