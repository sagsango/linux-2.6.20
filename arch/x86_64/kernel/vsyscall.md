```text
===============================================================================
FILE: arch/x86_64/kernel/vsyscall.c
PURPOSE: FIXED-ADDRESS FAST USERSPACE SYSTEM CALL HELPERS
KERNEL: Linux 2.6.x x86-64
===============================================================================

BACKGROUND
===============================================================================

Normal syscall path is expensive.

Example:

    gettimeofday()

If every call enters kernel mode:

    user mode
        |
        v
    syscall instruction
        |
        v
    kernel entry
        |
        v
    read time
        |
        v
    return to user mode

That costs hundreds/thousands of cycles.

But some operations mostly read kernel-maintained data:

    gettimeofday()
    time()
    getcpu()

So x86-64 Linux provided a fixed userspace page called:

    vsyscall page

This page contains small kernel-provided functions mapped into every process.

Userspace can call them like normal functions.

===============================================================================
VSYSCALL BIG IDEA
===============================================================================

Instead of:

    userspace -> kernel syscall -> return

do:

    userspace -> mapped readonly code/data -> return

This avoids full kernel entry for fast time queries.

===============================================================================
VSYSCALL ADDRESS LAYOUT
===============================================================================

Comment says:

    vsyscall 1 is located at -10MB
    vsyscall 2 is located at -10MB + 1024
    etc.

There are at most 4 vsyscalls.

Each slot is 1024 bytes apart.

Conceptually:

    VSYSCALL_BASE = -10MB

    slot 0: vgettimeofday
    slot 1: vtime
    slot 2: vgetcpu
    slot 3: venosys_1

===============================================================================
WHY ONLY 4 VSYSCALLS?
===============================================================================

Old ABI fixed this layout.

Older kernels would not properly return -ENOSYS for more slots.

So the file says:

    if we want more than four, we need vDSO

Modern Linux uses vDSO instead.

===============================================================================
VSYSCALL vs VDSO
===============================================================================

vsyscall:

    fixed virtual address
    very small number of functions
    old x86-64 ABI
    less flexible
    historically executable fixed page

vDSO:

    ELF shared object mapped into userspace
    randomized
    more flexible
    modern mechanism

===============================================================================
IMPORTANT MACRO
===============================================================================

#define __vsyscall(nr) \
    __attribute__((unused, __section__(".vsyscall_" #nr)))

This places a function into a special linker section:

    .vsyscall_0
    .vsyscall_1
    .vsyscall_2
    .vsyscall_3

The linker script places those sections at fixed vsyscall addresses.

===============================================================================
RELATION TO vmlinux.lds.S
===============================================================================

The linker script places:

    .vsyscall_0
    .vsyscall_1
    .vsyscall_2
    .vsyscall_3

at exact fixed locations.

It also creates special aliases for data:

    xtime
    vxtime
    jiffies
    sys_tz
    xtime_lock
    vgetcpu_mode

vsyscall.c uses these aliases.

===============================================================================
IMPORTANT GLOBALS
===============================================================================

__sysctl_vsyscall

    enables/disables fast vsyscall behavior.

-------------------------------------------------------------------------------

__xtime_lock

    seqlock used by userspace-readable time code.

-------------------------------------------------------------------------------

__vgetcpu_mode

    tells vgetcpu whether to use RDTSCP or LSL.

===============================================================================
WHY SEQLOCK IS USED
===============================================================================

Time variables can be updated while userspace reads them.

Need consistent snapshot.

Writer:

    updates time under seqlock

Reader:

    read_seqbegin()
    read values
    read_seqretry()

If update happened during read:

    retry

This gives fast lockless readers.

===============================================================================
vgettimeofday()
===============================================================================

Slot:

    __vsyscall(0)

Purpose:

    fast gettimeofday()

Flow:

    if vsyscall disabled:
        use real syscall fallback

    if tv:
        do_vgettimeofday(tv)

    if tz:
        copy timezone

    return 0

===============================================================================
do_vgettimeofday()
===============================================================================

Reads:

    __xtime.tv_sec
    __xtime.tv_nsec
    __vxtime mode data

Then interpolates time since last timer tick using:

    TSC

or:

    HPET

===============================================================================
TSC MODE
===============================================================================

If mode is not HPET:

    t = get_cycles_sync()

    usec += ((t - last_tsc) * tsc_quot) >> 32

Meaning:

    read fast CPU cycle counter

    convert cycles to microseconds

===============================================================================
HPET MODE
===============================================================================

If mode is HPET:

    read HPET counter through fixed mapping

    usec += ((counter - last) * quot) >> 32

HPET is more stable but slower than TSC.

===============================================================================
WHY timeval_normalize()
===============================================================================

If microseconds exceed 1,000,000:

    carry into seconds

Example:

    tv_usec = 1,500,000

becomes:

    tv_sec += 1
    tv_usec = 500,000

===============================================================================
vtime()
===============================================================================

Slot:

    __vsyscall(1)

Purpose:

    fast time()

If vsyscall disabled:

    fallback to real syscall

Otherwise:

    return __xtime.tv_sec

If user pointer t is non-NULL:

    *t = __xtime.tv_sec

===============================================================================
vgetcpu()
===============================================================================

Slot:

    __vsyscall(2)

Purpose:

    fast get current CPU and NUMA node.

Useful for userspace per-CPU/per-node caches.

Important caveat:

    result is not guaranteed unless CPU affinity pins the task.

Scheduler may move task immediately after result.

===============================================================================
vgetcpu FAST CACHE
===============================================================================

Argument:

    struct getcpu_cache *tcache

If supplied:

    cache result for one jiffy.

Logic:

    if tcache->blob[0] == __jiffies:
        reuse cached CPU/node

    else:
        recompute CPU/node

This avoids expensive CPU identification every call.

===============================================================================
vgetcpu RDTSCP MODE
===============================================================================

If CPU supports RDTSCP:

    rdtscp(dummy, dummy, p)

RDTSCP can return auxiliary data.

Kernel writes CPU/node info into RDTSCP AUX MSR.

Then userspace reads it quickly.

===============================================================================
vgetcpu LSL MODE
===============================================================================

If RDTSCP not available:

    asm("lsl %1,%0" : "=r"(p) : "r"(__PER_CPU_SEG));

This reads segment limit from GDT per-cpu segment.

Kernel stores CPU/node data in that descriptor limit.

Clever old trick.

===============================================================================
vsyscall_set_cpu()
===============================================================================

Called per CPU.

Purpose:

    program CPU/node information for vgetcpu.

If RDTSCP available:

    write_rdtscp_aux((node << 12) | cpu)

Also stores CPU/node in GDT per-cpu descriptor limit.

Layout:

    low 12 bits  = CPU number
    upper bits   = NUMA node

===============================================================================
CPU HOTPLUG SUPPORT
===============================================================================

When CPU comes online:

    cpu_vsyscall_notifier()
        |
        v
    smp_call_function_single(cpu, cpu_vsyscall_init)

This initializes vsyscall CPU/node info on that CPU.

===============================================================================
SYSCTL: kernel.vsyscall64
===============================================================================

If CONFIG_SYSCTL:

    /proc/sys/kernel/vsyscall64

controls:

    sysctl_vsyscall

If disabled, fast path falls back to real syscall.

===============================================================================
INTERESTING PATCHING TRICK
===============================================================================

The file contains labels:

    vsysc1
    vsysc2

around real syscall instructions.

When sysctl changes, kernel maps those instruction bytes and patches them:

    SYSCALL = 0x050f
    NOP2    = 0x9090

If fast vsyscall enabled:

    replace syscall fallback with NOP

If disabled:

    restore syscall instruction

===============================================================================
WHY PATCH SYSCALL TO NOP?
===============================================================================

In the fast path, fallback syscall is not needed.

Patching out syscall instruction avoids unnecessary overhead/path.

This is self-modifying kernel-provided userspace code.

===============================================================================
map_vsyscall()
===============================================================================

Maps the vsyscall page into the fixed virtual address.

Code:

    __set_fixmap(VSYSCALL_FIRST_PAGE,
                 physaddr_page0,
                 PAGE_KERNEL_VSYSCALL)

This makes the page visible to userspace at the fixed address.

===============================================================================
vsyscall_init()
===============================================================================

Initialization function.

Checks:

    vgettimeofday address == expected slot
    vtime address == expected slot
    vgetcpu address == expected slot
    VSYSCALL_ADDR(0) == fixmap address

Then:

    map_vsyscall()

    register sysctl

    initialize CPU/node data on all CPUs

    register hotplug notifier

===============================================================================
WHY BUG_ON ADDRESS CHECKS?
===============================================================================

vsyscall ABI requires exact addresses.

If linker placed functions incorrectly:

    userspace ABI breaks

So kernel refuses to boot silently with bad layout.

===============================================================================
COMPLETE gettimeofday FAST PATH
===============================================================================

userspace calls fixed address
      |
      v
vgettimeofday()
      |
      v
read seqlock sequence
      |
      v
read xtime
      |
      v
read TSC or HPET
      |
      v
convert offset
      |
      v
retry if seqlock changed
      |
      v
return timeval

No kernel mode switch.

===============================================================================
COMPLETE FALLBACK PATH
===============================================================================

vsyscall disabled
      |
      v
vgettimeofday()
      |
      v
real syscall instruction
      |
      v
kernel do_gettimeofday()
      |
      v
return to user

===============================================================================
COMPLETE getcpu FAST PATH
===============================================================================

userspace calls vgetcpu()
      |
      v
check tcache and jiffies
      |
      +--> cache valid:
      |       use cached CPU/node
      |
      +--> cache invalid:
              use RDTSCP or LSL
              update cache
      |
      v
write cpu/node to user pointers

===============================================================================
SECURITY NOTE
===============================================================================

Old fixed-address vsyscall became a security problem later.

Because it was always mapped at a predictable address.

Modern systems replaced it with randomized vDSO and/or emulate vsyscall.

But in Linux 2.6 era, this was a major performance optimization.

===============================================================================
RELATION TO time.c
===============================================================================

time.c updates:

    xtime
    vxtime
    jiffies
    sys_tz
    xtime_lock

vsyscall.c reads them from userspace-mapped code/data.

So time.c is writer.

vsyscall.c is fast reader.

===============================================================================
RELATION TO setup64.c
===============================================================================

setup64.c initializes syscall MSRs.

vsyscall.c uses real syscall fallback when fast path disabled.

Both are part of x86-64 syscall ABI support.

===============================================================================
RELATION TO linker script
===============================================================================

vmlinux.lds.S places:

    .vsyscall_0 at fixed slot 0
    .vsyscall_1 at fixed slot 1
    .vsyscall_2 at fixed slot 2
    .vsyscall_3 at fixed slot 3

Without linker script support, this file cannot work.

===============================================================================
MENTAL MODEL
===============================================================================

Think of vsyscall as:

    a tiny kernel-provided shared library

mapped into every process at a fixed address.

It exposes:

    fast time
    fast current CPU lookup

without entering the kernel for the common case.

===============================================================================
ONE SENTENCE SUMMARY
===============================================================================

arch/x86_64/kernel/vsyscall.c implements the old fixed-address x86-64
vsyscall ABI by placing fast gettimeofday(), time(), and getcpu() helpers
into special linker sections mapped into every process, reading kernel
timekeeping data through seqlock-protected aliases, using TSC/HPET or
RDTSCP/segment tricks for speed, and falling back to real syscalls when
the vsyscall fast path is disabled.
===============================================================================
```

