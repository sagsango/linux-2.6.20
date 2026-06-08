```text
===============================================================================
FILE: arch/x86_64/kernel/smp.c
PURPOSE: SMP IPIs, TLB SHOOTDOWN, REMOTE FUNCTION EXECUTION
KERNEL: Linux 2.6.x
===============================================================================

BACKGROUND
===============================================================================

This file is one of the most important SMP files in Linux.

Before SMP:

    1 CPU
    1 TLB
    1 scheduler
    1 execution stream

Life is easy.

===============================================================================

With SMP:

CPU0
CPU1
CPU2
CPU3

all execute simultaneously.

Each CPU has:

    registers
    caches
    TLB
    local APIC

Now Linux must answer:

    "How do CPUs talk to each other?"

The answer is:

    IPI

Inter Processor Interrupt

This file implements most of that communication.

===============================================================================
WHAT THIS FILE DOES
===============================================================================

Main responsibilities:

    1. TLB shootdown
    2. Reschedule IPIs
    3. Remote function execution
    4. Stop CPUs during reboot
    5. SMP synchronization

Think:

    "CPU messaging subsystem"

===============================================================================
WHY TLB SHOOTDOWN EXISTS
===============================================================================

Suppose:

CPU0 and CPU1 are running same process.

Both have TLB entry:

    VA 0x4000 -> PA 0x1000

------------------------------------------------------------

CPU0 executes:

    munmap()

or

    page protection change

Page table updated:

    VA 0x4000 invalid

------------------------------------------------------------

CPU0 flushes its TLB.

CPU1 still has:

    VA 0x4000 -> PA 0x1000

inside its TLB.

CPU1 can continue using stale translation.

BUG.

===============================================================================

Therefore:

CPU0 must tell CPU1:

    "Flush your TLB too."

This operation is called:

    TLB shootdown

===============================================================================
TLB SHOOTDOWN FLOW
===============================================================================
/* XXX: Memory has MESI porotocl, while TLB dont have that
 *      What about L1, L2, L3 -> We do write through if mmio or memory barrier
 */

CPU0
 |
 | page table modified
 |
 +--> flush_tlb_page()
 |
 +--> send IPI
 |
 +-----------------------> CPU1
                           |
                           v
                smp_invalidate_interrupt()
                           |
                           v
                    flush local TLB
                           |
                           v
                     acknowledge

===============================================================================
flush_state
===============================================================================

Per-CPU structure:

union smp_flush_state

Contains:

    flush_cpumask

    flush_mm

    flush_va

    lock

This is shared metadata describing:

    which address space

    which page

    which CPUs

must flush.

===============================================================================
WHY PER-CPU FLUSH STATE?
===============================================================================

Imagine:

CPU0 sending flush

CPU1 sending flush

CPU2 sending flush

simultaneously.

Each sender gets its own flush_state.

Avoids global contention.

===============================================================================
MMU STATES
===============================================================================

Linux tracks:

TLBSTATE_OK

    CPU actively using mm

------------------------------------------------------------

TLBSTATE_LAZY

    CPU not actively using user pages

===============================================================================
LAZY TLB
===============================================================================

Suppose CPU is running:

    idle thread

No userspace active.

No reason to keep user address space loaded.

CPU can enter:

    TLBSTATE_LAZY

Then flush IPIs can simply:

    leave_mm()

instead of flushing.

Much cheaper.

===============================================================================
leave_mm()
===============================================================================

Purpose:

    stop participating in user address space

Flow:

remove cpu bit from:

    mm->cpu_vm_mask

load:

    swapper_pg_dir

Result:

CPU now only uses kernel mappings.

===============================================================================
IMPORTANT SMP COMMENT
===============================================================================

The giant comment in this file explains the hardest race.

Question:

What if CPU changes mm while another CPU sends flush?

Example:

CPU0:
    switch_mm()

CPU1:
    send flush

At same time.

The ordering of:

    active_mm
    cpu_vm_mask
    cr3

must be carefully designed.

Otherwise:

    stale TLB entries

or

    missed flushes

occur.

The whole comment explains why Linux's ordering avoids this.

===============================================================================
smp_invalidate_interrupt()
===============================================================================

MOST IMPORTANT FUNCTION

This is the TLB shootdown interrupt handler.

Triggered by:

    INVALIDATE_TLB_VECTOR

Flow:

receive IPI
      |
      v
lookup sender
      |
      v
find flush_state
      |
      v
check mm
      |
      v
flush page or full TLB
      |
      v
ack APIC

===============================================================================
WHY sender IS COMPUTED FROM VECTOR?
===============================================================================

Code:

sender = ~regs->orig_rax
         - INVALIDATE_TLB_VECTOR_START

In this implementation:

different vectors correspond to different senders.

Reason:

avoid global flush metadata.

Each sender owns its own flush state.

===============================================================================
FULL FLUSH VS SINGLE PAGE FLUSH
===============================================================================

flush_va == FLUSH_ALL

means:

    flush entire TLB

calls:

    local_flush_tlb()

------------------------------------------------------------

Otherwise:

    flush one page

calls:

    __flush_tlb_one()

Much cheaper.

===============================================================================
flush_tlb_others()
===============================================================================

Core SMP TLB function.

Flow:

store flush info
      |
      v
send IPI
      |
      v
wait until all CPUs respond
      |
      v
cleanup

===============================================================================
WAITING LOOP
===============================================================================

while (!cpus_empty(mask))
    cpu_relax();

Meaning:

    wait until every CPU clears its bit

Each remote CPU:

    clears its own bit

when finished.

Simple SMP barrier.

===============================================================================
flush_tlb_current_task()
===============================================================================

Used when current task's address space changes.

Flow:

local flush

then:

remote flush

for all CPUs using same mm.

===============================================================================
flush_tlb_mm()
===============================================================================

Flush entire address space.

Example:

    exec()

    massive mapping changes

Flow:

all CPUs running this mm
        |
        v
flush TLB

===============================================================================
flush_tlb_page()
===============================================================================

Single-page shootdown.

Example:

    mprotect(page)

    unmap(page)

Only one VA invalidated.

Much cheaper.

===============================================================================
flush_tlb_all()
===============================================================================

Nuclear option.

Every CPU.

Every TLB.

Flow:

on_each_cpu()
      |
      v
do_flush_tlb_all()

===============================================================================
WHY SO EXPENSIVE?
===============================================================================

Every CPU:

    interrupt

    pipeline disruption

    TLB flush

    refill

Modern systems hate this.

Kernel tries very hard to avoid it.

===============================================================================
RESCHEDULE IPI
===============================================================================

Another important IPI.

Function:

    smp_send_reschedule()

Used by scheduler.

===============================================================================
EXAMPLE
===============================================================================

CPU0 running task A.

Higher priority task B wakes up.

Task B belongs on CPU1.

CPU0 sends:

    RESCHEDULE_VECTOR

to CPU1.

CPU1 receives interrupt.

Scheduler runs.

Task switch happens.

===============================================================================
FLOW
===============================================================================

CPU0
 |
 +--> wakeup task
 |
 +--> smp_send_reschedule(CPU1)
 |
 +------------------------> CPU1
                            |
                            v
               smp_reschedule_interrupt()
                            |
                            v
                  scheduler runs

===============================================================================
REMOTE FUNCTION EXECUTION
===============================================================================

Another major feature.

Question:

How can CPU0 force CPU3 to execute a function?

Answer:

    smp_call_function()

===============================================================================
WHY NEEDED?
===============================================================================

Examples:

    MTRR updates

    TLB operations

    cache maintenance

    performance counters

Need code to run on remote CPUs.

===============================================================================
call_data_struct
===============================================================================

Shared request structure:

    function pointer

    argument

    started count

    finished count

    wait flag

===============================================================================
FLOW
===============================================================================

CPU0
 |
 +--> smp_call_function(func)
 |
 +--> fill call_data
 |
 +--> send CALL_FUNCTION_VECTOR
 |
 +-----------------------> CPU1
                           CPU2
                           CPU3

===============================================================================
smp_call_function_interrupt()
===============================================================================

Remote CPU handler.

Flow:

receive IPI
      |
      v
read function pointer
      |
      v
atomic_inc(started)
      |
      v
execute function
      |
      v
atomic_inc(finished)

===============================================================================
WAIT MODE
===============================================================================

wait = 0

    fire and forget

CPU0 continues immediately.

------------------------------------------------------------

wait = 1

CPU0 waits until:

    finished == cpu_count

Used when operation must complete everywhere.

===============================================================================
smp_call_function_single()
===============================================================================

Targets one CPU only.

Example:

    execute function on CPU4

Flow:

CPU0
 |
 +--> send IPI only to CPU4
 |
 +--> wait optional

===============================================================================
WHY NOT JUST CALL FUNCTION DIRECTLY?
===============================================================================

Because each CPU has:

    private registers
    private TLB
    private APIC
    private MSRs

Sometimes code must execute on the target CPU itself.

===============================================================================
STOPPING CPUs
===============================================================================

Used during:

    reboot

    panic

    kexec

===============================================================================
smp_send_stop()
===============================================================================

Flow:

send stop request
        |
        v
all CPUs execute:
        |
        v
smp_really_stop_cpu()
        |
        v
halt forever

===============================================================================
CPU STOP FLOW
===============================================================================

CPU0
 |
 +--> smp_send_stop()
 |
 +-----------------------> CPU1 halt
 |
 +-----------------------> CPU2 halt
 |
 +-----------------------> CPU3 halt

Only reboot CPU survives.

===============================================================================
APIC RELATIONSHIP
===============================================================================

This entire file depends on:

    Local APIC

The APIC provides:

    send_IPI_mask()

    send_IPI_allbutself()

    interrupt vectors

Without APIC:

    no SMP communication

===============================================================================
FULL TLB SHOOTDOWN EXAMPLE
===============================================================================

CPU0:

    munmap(0x4000)

        |
        v

flush_tlb_page()

        |
        v

flush_tlb_others()

        |
        v

send IPI to CPU1 CPU2 CPU3

        |
        +-----------------> CPU1
        |                  flush page
        |
        +-----------------> CPU2
        |                  flush page
        |
        +-----------------> CPU3
                           flush page

        |
        v

all acknowledge

        |
        v

continue execution

===============================================================================
RELATION TO OTHER FILES
===============================================================================

setup64.c

    initializes CPUs

------------------------------------------------------------

smpboot.c

    starts AP CPUs

------------------------------------------------------------

sched.c

    uses reschedule IPIs

------------------------------------------------------------

mm/*.c

    uses TLB shootdowns

------------------------------------------------------------

apic.c

    actually sends IPIs

------------------------------------------------------------

reboot.c

    uses smp_send_stop()

===============================================================================
MENTAL MODEL
===============================================================================

If setup64.c makes CPUs exist,

then smp.c allows CPUs to cooperate.

This file is essentially:

    CPU messaging layer

with three major message types:

    "Flush your TLB"

    "Run scheduler"

    "Execute this function"

and one emergency message:

    "Stop immediately"

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/smp.c implements the core SMP communication
mechanisms of Linux, including TLB shootdown IPIs, scheduler
reschedule IPIs, remote function execution through
smp_call_function(), and CPU stop/reboot coordination, allowing
multiple processors to maintain consistent memory translations and
operate as a single coherent kernel. :contentReference[oaicite:0]{index=0}
===============================================================================
```

