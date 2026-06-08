```text
===============================================================================
FILE: arch/x86_64/kernel/stacktrace.c
PURPOSE: SAVE KERNEL STACK TRACE ADDRESSES INTO A BUFFER
KERNEL: Linux 2.6.x x86-64
===============================================================================

BACKGROUND
===============================================================================

A stack trace is a list of return addresses showing:

    "How did execution reach here?"

Example:

    function_c()
      called by function_b()
        called by function_a()
          called by sys_write()

A stack trace is useful for:

    debugging
    lockdep
    warnings
    memory leak tracking
    latency tracing
    profiling
    crash analysis

===============================================================================
BIG PICTURE
===============================================================================

Caller wants stack trace:

    save_stack_trace()

        |
        v

    dump_trace()

        |
        v

    callbacks in stacktrace_ops

        |
        v

    save addresses into trace->entries[]

===============================================================================
IMPORTANT STRUCTURE
===============================================================================

struct stack_trace

Conceptually contains:

    entries

        array of instruction addresses

    nr_entries

        how many addresses saved

    max_entries

        capacity of array

    skip

        number of initial frames to ignore

    all_contexts

        whether to include interrupt/exception stack contexts

===============================================================================
WHY SAVE ADDRESSES ONLY?
===============================================================================

This file does not print function names.

It saves raw addresses:

    0xffffffff80201234
    0xffffffff80204567
    0xffffffff8010abcd

Later another layer can symbolize them into names:

    schedule()
    mutex_lock()
    do_sys_open()

This separation is useful because:

    stack capture should be fast

    formatting can happen later

===============================================================================
MAIN FUNCTION
===============================================================================

void save_stack_trace(struct stack_trace *trace,
                      struct task_struct *task)

Purpose:

    Save stack-backtrace addresses for a given task.

Flow:

    dump_trace(task, NULL, NULL, &save_stack_ops, trace);

    trace->entries[trace->nr_entries++] = ULONG_MAX;

===============================================================================
WHAT IS dump_trace()?
===============================================================================

dump_trace() is the architecture stack unwinder.

It walks kernel stacks and calls callbacks for:

    stack sections
    return addresses
    warnings

This file does not know how to unwind x86-64 stacks itself.

Instead, it provides callback operations.

===============================================================================
CALLBACK DESIGN
===============================================================================

struct stacktrace_ops save_stack_ops = {
    .warning        = save_stack_warning,
    .warning_symbol = save_stack_warning_symbol,
    .stack          = save_stack_stack,
    .address        = save_stack_address,
};

dump_trace() walks the stack.

Whenever it finds something interesting, it calls these callbacks.

===============================================================================
CALLBACK FLOW
===============================================================================

dump_trace()
    |
    +--> ops->stack()
    |
    +--> ops->address(addr)
    |
    +--> ops->address(addr)
    |
    +--> ops->warning()
    |
    +--> ops->address(addr)

===============================================================================
save_stack_address()
===============================================================================

This is the most important callback.

Input:

    addr

Meaning:

    one return address found by stack unwinder

Code logic:

    if trace->skip > 0:
        trace->skip--
        return

    if trace->nr_entries < trace->max_entries - 1:
        trace->entries[trace->nr_entries++] = addr

===============================================================================
WHY trace->skip EXISTS
===============================================================================

Often you do not want the tracing helper itself included.

Example:

    save_stack_trace()
    kmalloc()
    driver_probe()
    ...

If caller sets:

    trace->skip = 1

then first address is skipped.

This removes uninteresting wrapper frames.

===============================================================================
WHY max_entries - 1?
===============================================================================

The last slot is reserved for:

    ULONG_MAX

sentinel marker.

So actual addresses are saved only while:

    nr_entries < max_entries - 1

===============================================================================
ULONG_MAX SENTINEL
===============================================================================

After dump_trace():

    trace->entries[trace->nr_entries++] = ULONG_MAX;

Meaning:

    end of stack trace

This lets later code know:

    no more entries

Example:

    entries[0] = addr1
    entries[1] = addr2
    entries[2] = ULONG_MAX

===============================================================================
save_stack_stack()
===============================================================================

Called when dump_trace() enters a new stack context.

Example stack contexts:

    task stack

    interrupt stack

    exception stack

    NMI stack

Logic:

    return trace->all_contexts ? 0 : -1;

Meaning:

    if all_contexts is true:
        allow all stack contexts

    else:
        stop/skip when changing context

===============================================================================
WHY all_contexts MATTERS
===============================================================================

x86-64 can have multiple stacks:

    normal task kernel stack

    IRQ stack

    NMI stack

    exception stack

A full trace may cross stack boundaries.

But some users only want the current normal context.

===============================================================================
WARNING CALLBACKS
===============================================================================

save_stack_warning()

save_stack_warning_symbol()

Both are empty.

Why?

This API only wants saved addresses.

It does not print warnings.

If unwinder sees suspicious stack data, this implementation ignores
those warnings.

===============================================================================
FULL FLOW EXAMPLE
===============================================================================

Caller:

    unsigned long entries[16];

    struct stack_trace trace = {
        .entries = entries,
        .max_entries = 16,
        .nr_entries = 0,
        .skip = 0,
        .all_contexts = 0,
    };

    save_stack_trace(&trace, current);

Flow:

    save_stack_trace()
        |
        v
    dump_trace()
        |
        v
    found address A
        |
        v
    save_stack_address(A)
        |
        v
    entries[0] = A

    found address B
        |
        v
    entries[1] = B

    done
        |
        v
    entries[2] = ULONG_MAX

===============================================================================
WHERE THIS IS USED
===============================================================================

Stack traces are used by subsystems such as:

    lock debugging

    memory leak tracking

    scheduler debugging

    tracing

    WARN_ON paths

    diagnostics

The key point:

    This file captures the trace.

Other code decides what to do with it.

===============================================================================
RELATION TO show_trace()
===============================================================================

show_trace():

    walks stack and prints addresses/symbols

save_stack_trace():

    walks stack and stores addresses

Both use the same conceptual unwinding machinery.

===============================================================================
RELATION TO process.c
===============================================================================

process.c has:

    show_regs()

    show_trace()

Those are for printing during oops/panic.

stacktrace.c provides a reusable API for saving traces into memory.

===============================================================================
RELATION TO NMI WATCHDOG
===============================================================================

NMI watchdog may dump stack traces when CPU is stuck.

That is usually print-oriented.

This file is buffer-oriented.

It is useful when code wants to store traces quietly for later analysis.

===============================================================================
MENTAL MODEL
===============================================================================

This file is not the stack unwinder itself.

It is an adapter.

    dump_trace()
        produces addresses

    stacktrace.c
        catches those addresses

    struct stack_trace
        stores them

Think:

    "convert stack walking callbacks into an address array"

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/stacktrace.c provides a small callback adapter around
the x86-64 stack unwinder: it calls dump_trace(), receives each discovered
return address through stacktrace_ops callbacks, saves those addresses into
a struct stack_trace buffer, respects skip/all_contexts controls, and
terminates the saved trace with ULONG_MAX for later debugging or tracing use.
===============================================================================
```

