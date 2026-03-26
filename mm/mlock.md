================================================================================
FILE: mlock.c — MEMORY LOCKING (mlock / munlock / mlockall)
SOURCE: user file :contentReference[oaicite:0]{index=0}
================================================================================


SECTION 0: WHAT THIS FILE DOES
================================================================================

This file implements:

    ✔ sys_mlock()
    ✔ sys_munlock()
    ✔ sys_mlockall()
    ✔ sys_munlockall()

Purpose:

    "Prevent selected pages from being swapped out"

So:

    NORMAL:
        page → can be reclaimed → swap

    AFTER mlock:
        page → MUST stay in RAM


================================================================================
SECTION 1: BIG PICTURE
================================================================================

User calls:

    mlock(addr, len)

Kernel does:

    1. validate limits (RLIMIT_MEMLOCK)
    2. walk VMAs covering range
    3. mark them VM_LOCKED
    4. bring pages into memory (fault them in)


--------------------------------------------------------------------------------
KEY IDEA:
--------------------------------------------------------------------------------

mlock ≠ just flag

mlock =

    ✔ set VM_LOCKED
    ✔ ensure pages are present (no lazy fault later)


================================================================================
SECTION 2: HIGH LEVEL FLOW
================================================================================

sys_mlock()
    |
    |-- check capability
    |-- align address + length
    |-- check RLIMIT_MEMLOCK
    |
    |-- do_mlock(start, len, ON)
    |
    |-- return


sys_munlock()
    |
    |-- do_mlock(start, len, OFF)


================================================================================
SECTION 3: CORE WORKER — do_mlock()
================================================================================

This function walks VMAs.


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

do_mlock(start, len, on):
    |
    |-- find first VMA covering start
    |
    |-- loop over VMAs:
    |       determine subrange
    |       call mlock_fixup()
    |
    |-- continue until range covered


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

Requested range:
    [--------LOCK RANGE--------]

VMAs:
    [---VMA1---][------VMA2------][--VMA3--]

Process:

    split + adjust each VMA
    apply VM_LOCKED


================================================================================
SECTION 4: CORE LOGIC — mlock_fixup()
================================================================================

This is the MOST IMPORTANT function.


--------------------------------------------------------------------------------
RESPONSIBILITY:
--------------------------------------------------------------------------------

    Apply locking/unlocking to a subrange inside a VMA


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

mlock_fixup(vma, start, end, newflags):
    |
    |-- try vma_merge()  (optimize)
    |
    |-- else:
    |       split_vma(start)
    |       split_vma(end)
    |
    |-- update vm_flags
    |
    |-- update mm->locked_vm
    |
    |-- if locking:
    |       make_pages_present()


================================================================================
SECTION 5: VMA SPLITTING (IMPORTANT)
================================================================================

Why splitting?

Because user may lock partial region:

    VMA:   [-----------------------]
    LOCK:       [---------]

We need:

    [---][LOCKED][---]


--------------------------------------------------------------------------------
ASCII:
--------------------------------------------------------------------------------

Before:
    VMA: [------------------------]

After split:
    [---][ LOCKED ][---]


================================================================================
SECTION 6: VM_LOCKED FLAG
================================================================================

Key operation:

    vma->vm_flags = newflags


When locking:

    newflags |= VM_LOCKED

When unlocking:

    newflags &= ~VM_LOCKED


--------------------------------------------------------------------------------
MEANING:
--------------------------------------------------------------------------------

VM_LOCKED → pages must not be swapped


================================================================================
SECTION 7: FORCING PAGES INTO MEMORY
================================================================================

CRITICAL LINE:

    make_pages_present(start, end)


--------------------------------------------------------------------------------
WHY?
--------------------------------------------------------------------------------

mlock must ensure:

    pages are not only "locked"
    but also "already in RAM"


Otherwise:

    page fault later could block → breaks guarantee


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

make_pages_present():
    |
    |-- walk page tables
    |-- trigger faults
    |-- allocate/load pages


================================================================================
SECTION 8: ACCOUNTING — mm->locked_vm
================================================================================

Kernel tracks:

    mm->locked_vm  (number of locked pages)


--------------------------------------------------------------------------------
UPDATE LOGIC:
--------------------------------------------------------------------------------

pages = (end - start) >> PAGE_SHIFT

if locking:
    pages = -pages

mm->locked_vm -= pages


--------------------------------------------------------------------------------
INTERPRETATION:
--------------------------------------------------------------------------------

locking:
    subtract negative → increases locked_vm

unlocking:
    subtract positive → decreases locked_vm


================================================================================
SECTION 9: RESOURCE LIMIT (VERY IMPORTANT)
================================================================================

sys_mlock():

    lock_limit = RLIMIT_MEMLOCK


Check:

    locked_pages + current_locked_vm <= limit


Unless:

    process has CAP_IPC_LOCK


--------------------------------------------------------------------------------
WHY?
--------------------------------------------------------------------------------

Prevent users from pinning entire RAM


================================================================================
SECTION 10: sys_mlockall()
================================================================================

Locks entire address space.


--------------------------------------------------------------------------------
FLAGS:
--------------------------------------------------------------------------------

MCL_CURRENT:
    lock existing mappings

MCL_FUTURE:
    future mappings auto-locked


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

do_mlockall(flags):
    |
    |-- if FUTURE:
    |       mm->def_flags = VM_LOCKED
    |
    |-- if CURRENT:
    |       iterate all VMAs:
    |           apply mlock_fixup()


================================================================================
SECTION 11: FUTURE MAPPINGS
================================================================================

If:

    MCL_FUTURE

Then:

    mm->def_flags = VM_LOCKED


--------------------------------------------------------------------------------
MEANING:
--------------------------------------------------------------------------------

Future mmap() calls will automatically:

    set VM_LOCKED


================================================================================
SECTION 12: sys_munlockall()
================================================================================

Equivalent to:

    remove VM_LOCKED from all VMAs


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

do_mlockall(0)


================================================================================
SECTION 13: SHARED MEMORY LOCKING
================================================================================

Functions:

    user_shm_lock()
    user_shm_unlock()


--------------------------------------------------------------------------------
PURPOSE:
--------------------------------------------------------------------------------

Track locked memory for shared memory objects


--------------------------------------------------------------------------------
FLOW:
--------------------------------------------------------------------------------

user_shm_lock():
    |
    |-- check limit
    |-- increment user->locked_shm


user_shm_unlock():
    |
    |-- decrement user->locked_shm


================================================================================
SECTION 14: COMPLETE FLOW
================================================================================

--------------------------------------------------------------------------------
mlock FLOW
--------------------------------------------------------------------------------

User:
    mlock(addr, len)

Kernel:

    sys_mlock()
        |
        +--> check limits
        |
        +--> do_mlock()
                |
                +--> for each VMA:
                        |
                        +--> mlock_fixup()
                                |
                                +--> split VMA
                                +--> set VM_LOCKED
                                +--> make_pages_present()
                                +--> update accounting


--------------------------------------------------------------------------------
ASCII MASTER FLOW:
--------------------------------------------------------------------------------

Virtual memory:

    [---A---][---B---][---C---]

User locks:

        [------]

Kernel transforms:

    [---A---][LOCKED][---C---]

And ensures:

    LOCKED region → pages in RAM


================================================================================
SECTION 15: INTERACTION WITH OTHER SUBSYSTEMS
================================================================================

mlock interacts with:

    ✔ page fault handler
    ✔ swap subsystem
    ✔ reclaim (kswapd cannot reclaim locked pages)
    ✔ mmap/mprotect (VMA flags)
    ✔ mempolicy (NUMA placement)


================================================================================
SECTION 16: IMPORTANT PROPERTIES
================================================================================

1) Locked pages:
       cannot be swapped

2) Lock applies at VMA level:
       via VM_LOCKED

3) Pages are faulted in immediately

4) Partial VMA locking supported (via split)

5) Subject to RLIMIT_MEMLOCK


================================================================================
SECTION 17: COMMON PITFALLS
================================================================================

1) mlock does NOT pin pages for DMA
   → different mechanism (get_user_pages)

2) mlock does NOT prevent reclaim of file cache globally
   → only for this mapping

3) Overusing mlock can:
   → exhaust memory
   → harm system performance


================================================================================
SECTION 18: INTERVIEW INSIGHTS
================================================================================

1) mlock = "no swap + pre-fault"

2) Uses:
       VM_LOCKED flag + make_pages_present()

3) Works by:
       splitting VMAs + updating flags

4) Controlled by:
       RLIMIT_MEMLOCK

5) Important difference:
       mlock vs pinning (GUP)


================================================================================
SECTION 19: ONE-LINE SUMMARY
================================================================================

    mlock.c prevents pages from being swapped by marking VMAs as locked,
    faulting pages into memory, and enforcing per-process memory limits.


================================================================================
END
================================================================================
