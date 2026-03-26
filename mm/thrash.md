/*
 * Linux 2.6.20 — mm/thrash.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (thrashing protection)
 *  - What thrashing means in the VM subsystem
 *  - Why a swap token exists
 *  - Token acquisition / passing algorithm
 *  - faultstamp, last_interval, token_priority
 *  - Relation to page faults, reclaim, and swap-heavy workloads
 *
 * Source: user-provided thrash.c
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
This file implements a very small but very important policy mechanism:

    TOKEN-BASED THRASHING PROTECTION

The problem it tries to solve is not ordinary swapping.
The problem is:

    TOO MANY TASKS FAULTING AND SWAPPING AT THE SAME TIME

When that happens, the machine may spend more time:
    - reclaiming pages
    - swapping pages in
    - swapping pages out
than actually letting any one task make forward progress.

That pathological state is called THRASHING.
*/


/*
So this file introduces a simple idea:

    pick one mm as the "favored" address space

and let it get special treatment so it can make progress instead of
being constantly interfered with by competing address spaces.

That favored mm is represented by:

    swap_token_mm
*/


/***************************************************************
 * 1. WHAT IS THRASHING?
 ***************************************************************/

/*
Thrashing happens when the working sets of active processes exceed the
available memory badly enough that pages are constantly being reclaimed
and faulted back in.

Classic symptoms:

    process A faults in page X
    process B runs and evicts page X
    process A runs and faults X again
    process C evicts something A needs
    repeat forever

Result:
    very high page fault rate
    high swap I/O
    poor throughput
    terrible latency
*/


/*
In other words:

    the system is doing memory movement work,
    but not enough useful computation.
*/


/***************************************************************
 * 2. WHY A TOKEN HELPS
 ***************************************************************/

/*
The key insight is:

    if one process keeps losing its pages before it can make progress,
    give it temporary preference.

Instead of letting all processes fight equally for memory all the time,
choose one mm_struct as the temporary winner.

This can reduce page bouncing and let one address space rebuild enough of
its working set to actually execute productively.
*/


/*
So the swap token is a kind of:

    "working set protection hint"

It does NOT eliminate reclaim.
It does NOT pin all pages forever.
It simply biases reclaim/fault behavior so the token holder gets a better
chance to survive swap-heavy contention.
*/


/***************************************************************
 * 3. FILE SIZE VS IMPORTANCE
 ***************************************************************/

/*
This file is tiny.

But conceptually it is important because it is pure VM policy.

It does not move pages itself.
It does not write to disk.
It does not walk page tables.

Instead it answers:

    "Which address space should be protected right now?"
*/


/***************************************************************
 * 4. GLOBAL STATE
 ***************************************************************/

static DEFINE_SPINLOCK(swap_token_lock);
struct mm_struct *swap_token_mm;
static unsigned int global_faults;

/*
Meaning:

swap_token_lock:
    protects the token state

swap_token_mm:
    the current mm_struct holding the token

global_faults:
    global logical counter of fault activity used to measure intervals
*/


/***************************************************************
 * 5. WHERE THE PER-MM STATE LIVES
 ***************************************************************/

/*
This file uses fields in struct mm_struct:

    mm->faultstamp
    mm->last_interval
    mm->token_priority

These fields are not declared here, but this file depends on them.

Meaning:

faultstamp:
    last observed global_faults value when this mm contended/acquired token

last_interval:
    previous gap between contentions/fault events for this mm

token_priority:
    score used to decide who deserves the token more
*/


/***************************************************************
 * 6. CORE IDEA OF THE ALGORITHM
 ***************************************************************/

/*
The algorithm is based on fault intervals.

If a task comes back for the token more quickly than before,
that is taken as a sign that it is under increasing pressure.

So:

    shorter interval since last contention
        -> raise priority

    longer interval
        -> lower priority

Then compare the current task's priority with the current token holder's
priority.

If current task deserves it more, pass the token.
*/


/***************************************************************
 * 7. grab_swap_token() — PURPOSE
 ***************************************************************/

/*
Function:

    grab_swap_token()

This is called when the current task is in a situation where it may want
swap-thrashing protection.

This function does NOT sleep.
It does NOT block waiting for the lock.
It tries quickly, updates policy, and returns.
*/


/***************************************************************
 * 8. STEP 1: global_faults++
 ***************************************************************/

/*
The first thing grab_swap_token() does is:

    global_faults++

Meaning:
    every such event advances a global logical timeline.

This is not wall-clock time.
It is a fault-pressure progress counter.

That is clever because it measures contention in terms of VM activity,
not elapsed time.
*/


/***************************************************************
 * 9. STEP 2: COMPUTE current_interval
 ***************************************************************/

/*
    current_interval = global_faults - current->mm->faultstamp;

Meaning:

How many global fault events have happened since this mm last recorded
its own token-related activity?

Small interval:
    this mm is coming back quickly
    suggests strong ongoing memory pressure

Large interval:
    pressure may be lower or more spread out
*/


/***************************************************************
 * 10. STEP 3: spin_trylock()
 ***************************************************************/

/*
    if (!spin_trylock(&swap_token_lock))
        return;

Important design choice:

This path refuses to block.
If token state is busy, we just skip the attempt.

Why?
    This is policy bookkeeping on a fault/reclaim-sensitive path.
    The kernel does not want extra waiting here.

So token grabbing is opportunistic, not mandatory.
*/


/***************************************************************
 * 11. FIRST COME FIRST SERVED CASE
 ***************************************************************/

/*
If no one currently holds the token:

    if (swap_token_mm == NULL) {
        current->mm->token_priority += 2;
        swap_token_mm = current->mm;
    }

Meaning:
    first contender gets the token immediately.

Also:
    priority is boosted when token is acquired.

That boost helps prevent token from bouncing away too quickly.
*/


/***************************************************************
 * 12. CASE: CURRENT MM IS NOT THE HOLDER
 ***************************************************************/

/*
If someone else holds the token:

    if (current->mm != swap_token_mm) {
        ... adjust current mm priority ...
        ... compare priorities ...
    }

This is the main competition case.
*/


/***************************************************************
 * 13. PRIORITY ADJUSTMENT RULE
 ***************************************************************/

/*
Rule:

    if (current_interval < current->mm->last_interval)
        token_priority++
    else
        token_priority-- (down to zero)

Interpretation:

If this task returns sooner than last time,
its pressure is getting worse,
so raise its claim on the token.

If it returns later than before,
its urgency is weaker,
so decay the priority.
*/


/*
This is a simple adaptive heuristic.
It tries to identify the task that is suffering the most repeated
contention and should be protected.
*/


/***************************************************************
 * 14. TOKEN PASSING RULE
 ***************************************************************/

/*
After adjusting priority:

    if (current->mm->token_priority > swap_token_mm->token_priority)
        current wins token

If current mm wins:
    current->mm->token_priority += 2;
    swap_token_mm = current->mm;

So token transfer is based on relative priority.
*/


/*
Important subtlety:

When a task acquires the token, its priority is boosted again.

Why?
    To avoid token bouncing back and forth too aggressively.

Without that boost:
    tiny short-term fluctuations might move token constantly.

With boost:
    token holder gets a chance to stabilize and make real progress.
*/


/***************************************************************
 * 15. CASE: TOKEN HOLDER COMES IN AGAIN
 ***************************************************************/

/*
Else branch:

    else {
        current->mm->token_priority += 2;
    }

Meaning:
    if the current holder comes back again under pressure,
    strengthen its hold on the token.

This reinforces locality and reduces token churn.
*/


/***************************************************************
 * 16. STEP 4: UPDATE STAMPS
 ***************************************************************/

/*
At the end of the function:

    current->mm->faultstamp = global_faults;
    current->mm->last_interval = current_interval;

Meaning:
    record this attempt as the new baseline for the next comparison.
*/


/*
So each mm keeps a memory of:

    - when it last contended
    - how far apart its recent contentions are

That gives the algorithm a tiny bit of historical feedback.
*/


/***************************************************************
 * 17. __put_swap_token() — PROCESS EXIT PATH
 ***************************************************************/

/*
Function:

    __put_swap_token(struct mm_struct *mm)

Purpose:

    called on process exit / mm teardown

If exiting mm currently holds token:
    clear swap_token_mm = NULL
*/


/*
This is necessary because token state points to mm_struct.
If holder exits, token must not dangle.
*/


/***************************************************************
 * 18. WHAT THIS FILE DOES NOT DO
 ***************************************************************/

/*
This file does NOT:

    - scan LRUs
    - allocate pages
    - write swap I/O
    - fault pages in
    - walk page tables

Those jobs belong to files like:

    vmscan.c
    swap.c
    swap_state.c
    swapfile.c
    page_io.c
    rmap.c

This file only decides:

    who should get special anti-thrashing preference
*/


/***************************************************************
 * 19. HOW IT FITS INTO THE REST OF SWAP/RECLAIM
 ***************************************************************/

/*
Big picture flow:

heavy memory pressure
    ↓
frequent faults + reclaim + swap traffic
    ↓
some mm keeps faulting too quickly
    ↓
grab_swap_token() notices short intervals
    ↓
that mm priority rises
    ↓
it may become swap_token_mm
    ↓
other VM code can prefer protecting token holder's working set
*/


/*
So the token is a coordination signal used by the rest of the VM system.
*/


/***************************************************************
 * 20. WHY global_faults INSTEAD OF jiffies?
 ***************************************************************/

/*
The file includes linux/jiffies.h, but the implemented algorithm uses
global_faults rather than wall-clock time.

That is actually a nice design choice:

    thrashing is about fault pressure,
    not raw elapsed time.

A machine can be idle for long periods; that should not distort the model.
But repeated fault events do indicate actual contention.
*/


/***************************************************************
 * 21. WHY spin_trylock() INSTEAD OF spin_lock()?
 ***************************************************************/

/*
Again, this is a policy fast-path design choice.

On fault/reclaim-related paths, the kernel prefers:

    "best effort and move on"

rather than:

    "everyone stop and serialize on token policy"

So the algorithm tolerates missed updates in exchange for low overhead.
*/


/***************************************************************
 * 22. STRENGTHS OF THIS DESIGN
 ***************************************************************/

/*
1. Very small amount of state
2. Cheap lock footprint
3. Adaptive to recurring pressure
4. Helps reduce token bouncing
5. Better chance that one task makes progress under thrash
*/


/***************************************************************
 * 23. LIMITATIONS OF THIS DESIGN
 ***************************************************************/

/*
1. Very heuristic
   It does not model full working-set sizes.

2. Single token
   One favored mm may not capture all useful system behavior.

3. Best-effort updates only
   spin_trylock means some observations are skipped.

4. Depends on other VM code respecting the token
   This file alone does not fix thrashing.
*/


/***************************************************************
 * 24. END-TO-END MENTAL MODEL
 ***************************************************************/

/*
Imagine three tasks all faulting heavily:

    A faults often
    B faults often
    C faults occasionally

Suppose A keeps returning for memory help sooner and sooner.
Then:

    A's current_interval gets smaller than previous interval
    → A.token_priority rises

If A.priority exceeds current holder's priority:
    token moves to A

If A continues to come back while holding token:
    A.priority rises further
    → token stays with A a little longer

That stabilizes protection and gives A a chance to rebuild useful
working set instead of losing pages immediately.
*/


/***************************************************************
 * 25. INTERVIEW MENTAL MODEL
 ***************************************************************/

/*
If asked:

"What does mm/thrash.c do?"

Good answer:

    It implements a lightweight token-based anti-thrashing policy.
    Each mm tracks recent fault intervals and a token priority. When an
    address space contends for memory more frequently than before, its
    priority rises. The highest-priority mm can become the current
    swap-token holder, allowing the VM subsystem to preferentially protect
    that address space so it can make forward progress under heavy swap
    pressure.
*/


/***************************************************************
 * 26. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/thrash.c implements a simple adaptive swap-token policy that picks one
mm_struct to preferentially protect during heavy fault/swap contention,
reducing pathological thrashing.
*/


/***************************************************************
 * END
 ***************************************************************/

