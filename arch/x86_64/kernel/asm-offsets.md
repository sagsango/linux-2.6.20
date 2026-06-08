```text
===============================================================================
ASM-OFFSETS GENERATOR
File: arch/x86_64/kernel/asm-offsets.c
===============================================================================

PURPOSE
=======

This file does NOT run inside the kernel at runtime.

It is a build-time helper.

Its job:

    Generate C structure offsets for assembly files.

Why?

Assembly code cannot understand C structures directly.

C code can write:

    current->thread.rsp

But assembly needs numeric offsets:

    movq  offset(%rax), %rsp

So this file calculates those offsets using C compiler knowledge.


===============================================================================
MAIN PROBLEM
===============================================================================

C structure:

struct task_struct {
    ...
    long state;
    unsigned long flags;
    struct thread_struct thread;
    pid_t pid;
    ...
};

C code can say:

    task->state

Assembly cannot say:

    task->state

Assembly needs:

    task + OFFSET_OF_STATE

Example:

    movq tsk_state(%rax), %rbx


So we need generated constants:

    #define tsk_state   0
    #define tsk_flags   8
    #define tsk_thread  1024
    #define tsk_pid     1500

The real numbers depend on compiler, config, padding, and architecture.


===============================================================================
WHY NOT HARDCODE OFFSETS?
===============================================================================

Bad idea:

    #define tsk_state 0
    #define tsk_flags 8

Because structure layout can change due to:

    - kernel config options
    - compiler padding
    - architecture differences
    - field additions/removals
    - alignment rules

So Linux asks the C compiler:

    "What is the actual offset of this field?"

That is exactly what offsetof() does.


===============================================================================
BUILD-TIME FLOW
===============================================================================

During kernel build:

    asm-offsets.c
          |
          v
    compiled as small helper program
          |
          v
    compiler expands offsetof()
          |
          v
    inline asm emits special lines
          |
          v
    build scripts parse output
          |
          v
    generated header created
          |
          v
    assembly files include generated offsets


Flow:

+-------------------+
| C Structures      |
| task_struct       |
| thread_info       |
| x8664_pda         |
+---------+---------+
          |
          v
+-------------------+
| asm-offsets.c     |
| offsetof()        |
+---------+---------+
          |
          v
+-------------------+
| asm output        |
| ->tsk_state 0     |
| ->tsk_flags 8     |
+---------+---------+
          |
          v
+-------------------+
| generated header  |
+---------+---------+
          |
          v
+-------------------+
| assembly code     |
+-------------------+


===============================================================================
IMPORTANT MACROS
===============================================================================

DEFINE(sym, val)
----------------

#define DEFINE(sym, val) \
    asm volatile("\n->" #sym " %0 " #val : : "i" (val))

This emits a special marker into assembly output.

Example:

    DEFINE(tsk_state, offsetof(struct task_struct, state))

may emit:

    ->tsk_state 0 offsetof(struct task_struct, state)


The build system later extracts lines starting with:

    ->


BLANK()
-------

#define BLANK() asm volatile("\n->" : : )

Emits a blank separator line.

Used only for readability in generated output.


===============================================================================
MAIN FUNCTION
===============================================================================

int main(void)
{
    ...
    return 0;
}

This is not a normal application.

It exists so the compiler can process the C code and emit assembly.

The program itself is not important.

The generated assembly output is important.


===============================================================================
PART 1 : task_struct OFFSETS
===============================================================================

Code:

#define ENTRY(entry) DEFINE(tsk_ ## entry, offsetof(struct task_struct, entry))

    ENTRY(state);
    ENTRY(flags);
    ENTRY(thread);
    ENTRY(pid);

Generated names:

    tsk_state
    tsk_flags
    tsk_thread
    tsk_pid


Meaning:

    tsk_state  = offset of task_struct.state
    tsk_flags  = offset of task_struct.flags
    tsk_thread = offset of task_struct.thread
    tsk_pid    = offset of task_struct.pid


Why assembly needs this?

Context switch code, syscall entry code, and scheduler assembly may need
to access current task fields.


Example idea:

    current task pointer
          |
          v
    task_struct
          |
          +--> state
          +--> flags
          +--> thread
          +--> pid


Assembly style:

    movq tsk_thread(%rax), %rbx


===============================================================================
PART 2 : thread_info OFFSETS
===============================================================================

Code:

#define ENTRY(entry) DEFINE(threadinfo_ ## entry, offsetof(struct thread_info, entry))

    ENTRY(flags);
    ENTRY(addr_limit);
    ENTRY(preempt_count);
    ENTRY(status);


Generated names:

    threadinfo_flags
    threadinfo_addr_limit
    threadinfo_preempt_count
    threadinfo_status


thread_info is important for low-level entry/exit code.

Common use:

    - check pending work
    - check need reschedule
    - check signal pending
    - check preemption count
    - check user/kernel address limit


Memory idea:

+-------------------------+
| thread_info             |
+-------------------------+
| flags                   |
| addr_limit              |
| preempt_count           |
| status                  |
+-------------------------+


===============================================================================
PART 3 : x8664_pda OFFSETS
===============================================================================

Code:

#define ENTRY(entry) DEFINE(pda_ ## entry, offsetof(struct x8664_pda, entry))

    ENTRY(kernelstack);
    ENTRY(oldrsp);
    ENTRY(pcurrent);
    ENTRY(irqcount);
    ENTRY(cpunumber);
    ENTRY(irqstackptr);
    ENTRY(data_offset);


Generated names:

    pda_kernelstack
    pda_oldrsp
    pda_pcurrent
    pda_irqcount
    pda_cpunumber
    pda_irqstackptr
    pda_data_offset


What is PDA?

PDA = Per-CPU Data Area

On x86-64 old kernels, PDA stores important per-CPU state.

Each CPU has its own PDA.

CPU0:

+-------------------------+
| x8664_pda               |
+-------------------------+
| kernelstack             |
| oldrsp                  |
| pcurrent                |
| irqcount                |
| cpunumber               |
| irqstackptr             |
| data_offset             |
+-------------------------+

CPU1:

+-------------------------+
| x8664_pda               |
+-------------------------+
| kernelstack             |
| oldrsp                  |
| pcurrent                |
| irqcount                |
| cpunumber               |
| irqstackptr             |
| data_offset             |
+-------------------------+


Why assembly needs PDA?

Low-level interrupt/syscall entry needs fast per-CPU access:

    current task
    kernel stack
    IRQ stack
    CPU number


===============================================================================
PART 4 : IA32 EMULATION OFFSETS
===============================================================================

Enabled only if:

    CONFIG_IA32_EMULATION


Purpose:

Support 32-bit programs on 64-bit kernel.

Code generates offsets for:

    struct sigcontext_ia32


Fields:

    eax
    ebx
    ecx
    edx
    esi
    edi
    ebp
    esp
    eip


Generated names:

    IA32_SIGCONTEXT_eax
    IA32_SIGCONTEXT_ebx
    IA32_SIGCONTEXT_ecx
    IA32_SIGCONTEXT_edx
    IA32_SIGCONTEXT_esi
    IA32_SIGCONTEXT_edi
    IA32_SIGCONTEXT_ebp
    IA32_SIGCONTEXT_esp
    IA32_SIGCONTEXT_eip


Why needed?

Signal return / signal delivery code may be partly assembly.

When a 32-bit process receives a signal on 64-bit Linux,
kernel builds a 32-bit signal frame.

Assembly needs exact register offsets.


Also generated:

    IA32_RT_SIGFRAME_sigcontext

This is offset of:

    struct rt_sigframe32.uc.uc_mcontext


===============================================================================
PART 5 : SUSPEND PAGE BACKUP ENTRY OFFSETS
===============================================================================

Code:

DEFINE(pbe_address,      offsetof(struct pbe, address));
DEFINE(pbe_orig_address, offsetof(struct pbe, orig_address));
DEFINE(pbe_next,         offsetof(struct pbe, next));


pbe means:

    Page Backup Entry

Used in suspend/hibernate code.


Fields:

    address
    orig_address
    next


Diagram:

+----------------------+
| struct pbe           |
+----------------------+
| address              |
| orig_address         |
| next                 |
+----------------------+


Why assembly needs this?

Low-level suspend/restore code may walk page backup lists while normal C
environment is not fully available.


===============================================================================
PART 6 : TSS IST OFFSET
===============================================================================

Code:

DEFINE(TSS_ist, offsetof(struct tss_struct, ist));


TSS = Task State Segment

IST = Interrupt Stack Table

On x86-64, IST provides special stacks for critical exceptions.

Examples:

    NMI
    Double Fault
    Machine Check


Why needed?

Low-level exception entry code must know where IST entries live inside TSS.


Diagram:

+----------------------+
| TSS                  |
+----------------------+
| ...                  |
| IST[0]               |
| IST[1]               |
| IST[2]               |
| ...                  |
+----------------------+


===============================================================================
PART 7 : CRYPTO CONTEXT OFFSET
===============================================================================

Code:

DEFINE(crypto_tfm_ctx_offset, offsetof(struct crypto_tfm, __crt_ctx));


Meaning:

Offset of crypto transform private context.

Why assembly may need it?

Some optimized crypto routines are written in assembly and need to reach
the algorithm context from struct crypto_tfm.


===============================================================================
GENERATED OUTPUT EXAMPLE
===============================================================================

The compiler may emit lines like:

->tsk_state 0 offsetof(struct task_struct, state)
->tsk_flags 8 offsetof(struct task_struct, flags)
->tsk_thread 720 offsetof(struct task_struct, thread)
->threadinfo_flags 0 offsetof(struct thread_info, flags)
->pda_kernelstack 0 offsetof(struct x8664_pda, kernelstack)
->TSS_ist 36 offsetof(struct tss_struct, ist)


Then build scripts turn this into assembly-usable definitions:

#define tsk_state 0
#define tsk_flags 8
#define tsk_thread 720
#define threadinfo_flags 0
#define pda_kernelstack 0
#define TSS_ist 36


===============================================================================
WHY INLINE ASM?
===============================================================================

This line:

    asm volatile("\n->" #sym " %0 " #val : : "i" (val))

does not create useful machine code.

It creates recognizable text in compiler assembly output.

The special marker is:

    ->

Build scripts grep these lines and convert them into headers.


This is a clever trick:

    Use the C compiler to compute offsets,
    then export them to assembly.


===============================================================================
WHO USES THESE OFFSETS?
===============================================================================

Likely users:

    entry.S
    syscall entry code
    interrupt entry code
    context switch code
    signal trampoline code
    suspend/resume assembly
    crypto assembly


Examples:

1. Syscall entry needs current task:

    pda_pcurrent

2. Interrupt entry needs IRQ stack:

    pda_irqstackptr

3. Return to user checks thread flags:

    threadinfo_flags

4. Context switch touches task thread state:

    tsk_thread

5. Signal return restores IA32 registers:

    IA32_SIGCONTEXT_eip


===============================================================================
FULL FLOW
===============================================================================

Kernel Build
      |
      v
Compile asm-offsets.c
      |
      v
C compiler computes offsetof()
      |
      v
Inline asm emits special lines
      |
      v
Build script extracts "->" lines
      |
      v
Generated offset header
      |
      v
Assembly files include constants
      |
      v
Kernel assembly can safely access C structs


===============================================================================
KEY IDEA
===============================================================================

This file is a bridge between C and assembly.

C knows structures by field names:

    task->state

Assembly only knows numbers:

    task pointer + offset

asm-offsets.c automatically generates those numbers so assembly code does
not break when C structures change.

Without this file:

    assembly code would need hardcoded offsets,
    and every structure layout change could silently break the kernel.
===============================================================================
```

