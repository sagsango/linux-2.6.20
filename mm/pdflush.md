/*
 * Linux 2.6.20 — mm/pdflush.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - Detailed background
 *  - Why pdflush exists
 *  - Thread-pool design
 *  - Full flow from page-writeback.c into pdflush.c
 *  - Function-by-function walkthrough
 *  - Important concurrency ideas
 *
 * Source basis: uploaded Linux 2.6.20 mm/pdflush.c
 */


/***************************************************************
 * 0. BIG PICTURE
 ***************************************************************

/*
Where pdflush sits:

    User write() / mmap write
            |
            v
    Dirty pages accumulate in page cache
            |
            v
    page-writeback.c decides:
        - start background writeback
        - periodic writeback
        - sometimes writer itself writes back
            |
            v
    pdflush_operation(fn, arg)
            |
            v
    pdflush thread pool
            |
            v
    callback executes:
        background_writeout(arg)
        wb_kupdate(arg)
        laptop_flush(arg)
        ...
            |
            v
    writeback_inodes()
            |
            v
    filesystem writepage()/writepages()
            |
            v
    BIO / block layer / disk
*/


/***************************************************************
 * 1. WHY pdflush EXISTS
 ***************************************************************

/*
Problem:
    Dirty pages must be written out in the background.

But we do NOT want:
    - random user processes doing all background writeback
    - one dedicated single thread for the whole machine
    - uncontrolled many threads per dirtying event

Need:
    - a small reusable pool of worker threads
    - can run writeback jobs asynchronously
    - can scale somewhat with load
    - can shrink back down when idle

That is what pdflush is in Linux 2.6.20.
*/


/*
Historical note:
    Older kernels used pdflush as the generic background writeback worker pool.
    Later kernels moved toward per-bdi flusher threads, which scale better.

But in 2.6.20, pdflush is central to writeback design.
*/


/***************************************************************
 * 2. WHAT pdflush IS
 ***************************************************************

/*
pdflush is NOT the writeback logic itself.

It is a THREAD POOL EXECUTION LAYER.

Meaning:
    - page-writeback.c decides WHEN work should happen
    - pdflush.c finds an idle worker thread
    - the worker executes a callback function

So pdflush.c is like a dispatcher + worker-pool manager.
*/


/***************************************************************
 * 3. DESIGN GOALS
 ***************************************************************

/*
From the code/comments, the design tries to achieve:

1. Keep some workers always available
   → MIN_PDFLUSH_THREADS

2. Do not create too many workers
   → MAX_PDFLUSH_THREADS

3. If no idle threads exist for a while, grow pool

4. If threads have been idle for a while, shrink pool

5. Reuse sleeping workers via a global idle list
*/


/***************************************************************
 * 4. IMPORTANT CONSTANTS
 ***************************************************************

#define MIN_PDFLUSH_THREADS 2
#define MAX_PDFLUSH_THREADS 8

/*
Interpretation:
    - Keep at least 2 pdflush workers alive
    - Allow growth up to 8

Why not 1?
    - background writeback can overlap
    - avoids total starvation if one thread blocks

Why not huge?
    - writeback concurrency must be controlled
    - too many threads cause contention and disk thrashing
*/


/***************************************************************
 * 5. IMPORTANT GLOBALS
 ***************************************************************

/*
static LIST_HEAD(pdflush_list);
    - list of IDLE pdflush workers
    - only idle workers are on this list

static DEFINE_SPINLOCK(pdflush_lock);
    - protects pdflush_list and related state

int nr_pdflush_threads;
    - total count of existing pdflush workers

static unsigned long last_empty_jifs;
    - records when idle worker list became empty
    - used for growth heuristic
*/


/***************************************************************
 * 6. THE CORE DATA STRUCTURE
 ***************************************************************

struct pdflush_work {
    struct task_struct *who;
    void (*fn)(unsigned long);
    unsigned long arg0;
    struct list_head list;
    unsigned long when_i_went_to_sleep;
};

/*
Meaning of fields:

who:
    actual kernel thread task_struct

fn:
    callback to run
    ex: background_writeout, wb_kupdate, laptop_flush

arg0:
    callback argument

list:
    used when worker is idle on pdflush_list

when_i_went_to_sleep:
    timestamp of when it entered idle state
    used to decide when to shrink the pool
*/


/***************************************************************
 * 7. MAIN MENTAL MODEL
 ***************************************************************

/*
Each pdflush thread repeatedly does:

    become idle
    put self onto idle list
    sleep
    wake up when assigned work
    run callback
    maybe trigger pool growth
    maybe self-terminate if excess idle capacity exists
    repeat
*/


/***************************************************************
 * 8. FULL FLOW OF A JOB
 ***************************************************************

/*
Example path:

page-writeback.c:
    pdflush_operation(background_writeout, 0)

pdflush_operation():
    - grabs pdflush_lock
    - takes one idle worker from pdflush_list
    - removes it from idle list
    - stores fn and arg0 into that worker's pdflush_work
    - wake_up_process(worker)

worker thread wakes:
    - sees fn != NULL
    - drops lock
    - executes fn(arg0)
      ex: background_writeout(0)

callback completes:
    - worker re-enters idle state
    - may grow pool if there were no idle threads for >1 second
    - may exit if too many threads have been idle for >1 second
*/


/***************************************************************
 * 9. __pdflush() — THE REAL WORKER LOOP
 ***************************************************************

/*
Function:
    static int __pdflush(struct pdflush_work *my_work)

This is the heart of the file.
*/


/*
Step 1: thread role flags

    current->flags |= PF_FLUSHER | PF_SWAPWRITE;

PF_FLUSHER:
    marks this task as a flusher thread
    other code can treat it specially

PF_SWAPWRITE:
    allows access to filesystem/memory reclaim paths that may otherwise
    restrict ordinary tasks; flusher threads may need to write pages under
    memory pressure
*/


/*
Step 2: initialize worker-local control block

    my_work->fn = NULL;
    my_work->who = current;
    INIT_LIST_HEAD(&my_work->list);
*/


/*
Step 3: increment global thread count under lock

    nr_pdflush_threads++;
*/


/*
Step 4: main infinite loop

for (;;) {
    set_current_state(TASK_INTERRUPTIBLE);
    list_move(&my_work->list, &pdflush_list);
    my_work->when_i_went_to_sleep = jiffies;
    spin_unlock_irq(&pdflush_lock);
    schedule();
    try_to_freeze();
    spin_lock_irq(&pdflush_lock);

    ... validate wakeup ...
    ... execute callback ...
    ... maybe grow/shrink pool ...
}
*/


/***************************************************************
 * 10. IDLE PATH
 ***************************************************************

/*
When the worker has no job:

    set_current_state(TASK_INTERRUPTIBLE)
        → mark self sleepable

    list_move(&my_work->list, &pdflush_list)
        → publish self as an idle worker

    when_i_went_to_sleep = jiffies
        → remember idle timestamp

    spin_unlock_irq(&pdflush_lock)
    schedule()
        → actually sleep
*/


/*
This is very important:

    pdflush_list contains IDLE workers only.

A worker is available for assignment only while it is on that list.
*/


/***************************************************************
 * 11. FREEZER INTERACTION
 ***************************************************************

/*
After schedule():

    try_to_freeze();

This is for suspend / hibernation cooperation.
Kernel threads often need freezer support so system suspend can quiesce them.

The comment mentions swsusp and refrigerator() interactions.
*/


/***************************************************************
 * 12. BOGUS WAKEUP HANDLING
 ***************************************************************

/*
After waking, code checks:

    if (!list_empty(&my_work->list)) {
        my_work->fn = NULL;
        continue;
    }

Meaning:
    - thread woke up
    - but its work item is STILL on idle list
    - therefore no real job was assigned

Comment says swsusp/freezer may cause this kind of wakeup.
*/


/*
Then:

    if (my_work->fn == NULL) {
        printk("pdflush: bogus wakeup\n");
        continue;
    }

Meaning:
    - worker was removed from idle list
    - but no callback was set
    - should not normally happen
*/


/***************************************************************
 * 13. EXECUTION PATH
 ***************************************************************

/*
Normal case:

    spin_unlock_irq(&pdflush_lock);
    (*my_work->fn)(my_work->arg0);

Important:
    callback runs WITHOUT pdflush_lock held.

Why?
    - callback may take long time
    - callback may sleep/block/do I/O
    - cannot hold global spinlock across that
*/


/***************************************************************
 * 14. GROWTH HEURISTIC
 ***************************************************************

/*
After callback returns:

    if (jiffies - last_empty_jifs > 1 * HZ) {
        if (list_empty(&pdflush_list)) {
            if (nr_pdflush_threads < MAX_PDFLUSH_THREADS)
                start_one_pdflush_thread();
        }
    }

Meaning:
    if there have been NO idle workers for > 1 second,
    and still none are idle now,
    and we are below max,
    then create another pdflush thread.
*/


/*
Interpretation:
    sustained demand with no idle capacity → grow pool slowly

Why wait 1 second?
    to avoid overreacting to tiny transient spikes
*/


/***************************************************************
 * 15. SHRINK HEURISTIC
 ***************************************************************

/*
After re-taking pdflush_lock:

    my_work->fn = NULL;

    if (list_empty(&pdflush_list))
        continue;
    if (nr_pdflush_threads <= MIN_PDFLUSH_THREADS)
        continue;

    pdf = list_entry(pdflush_list.prev, struct pdflush_work, list);
    if (jiffies - pdf->when_i_went_to_sleep > 1 * HZ) {
        pdf->when_i_went_to_sleep = jiffies;
        break;
    }

Meaning:
    if there is at least one idle worker,
    and thread count is above minimum,
    and the "sleepiest" worker has been idle >1 second,
    then exit one thread.
*/


/*
Important subtlety:
    list_entry(pdflush_list.prev, ...)

This code picks the least-recently-went-to-sleep thread based on list order.
So the thread pool trims long-idle capacity first.
*/


/*
On break:
    nr_pdflush_threads--;
    return 0;

That worker thread exits.
*/


/***************************************************************
 * 16. WHY my_work IS OUTSIDE __pdflush() STACK LOCALS
 ***************************************************************

/*
The comment says my_work could logically be a local variable in __pdflush(),

But it is separated in pdflush() and passed into __pdflush() to avoid
compiler optimizations doing something "unfortunate" with auto variables
visible to other CPUs/tasks.

This is basically defensive kernel paranoia about concurrency and compiler
behavior around stack locals whose addresses escape.
*/


/***************************************************************
 * 17. pdflush() WRAPPER
 ***************************************************************

/*
Function:
    static int pdflush(void *dummy)

This wrapper prepares the thread environment, then calls __pdflush().
*/


/*
Step 1: nice level

    set_user_nice(current, 0);

Reason from comment:
    pdflush may spend time doing expensive work (ex: dm-crypt encryption)
    so do not inherit weird low-priority worker settings from kthread parent.
*/


/*
Step 2: CPU affinity / cpuset handling

    cpus_allowed = cpuset_cpus_allowed(current);
    set_cpus_allowed(current, cpus_allowed);

Meaning:
    restore affinity according to cpuset rules for this dynamically created
    kernel thread.

This matters because kthread creation path may temporarily override to
CPU_MASK_ALL.
*/


/*
Then:

    return __pdflush(&my_work);
*/


/***************************************************************
 * 18. pdflush_operation() — DISPATCH API
 ***************************************************************

/*
Function:
    int pdflush_operation(void (*fn)(unsigned long), unsigned long arg0)

This is the public entry point used by page-writeback.c.
*/


/*
Flow:

    BUG_ON(fn == NULL)

    spin_lock_irqsave(&pdflush_lock, flags)

    if no idle workers:
        return -1

    else:
        pick first idle worker from pdflush_list
        list_del_init(&pdf->list)

        if idle list became empty:
            last_empty_jifs = jiffies

        pdf->fn = fn
        pdf->arg0 = arg0
        wake_up_process(pdf->who)
*/


/*
Key point:
    pdflush_operation() does NOT queue work if no idle thread exists.
    It simply returns -1.

So caller must tolerate failure to dispatch asynchronously.

Example:
    wb_timer_fn() in page-writeback.c checks if pdflush_operation() failed,
    and delays/retries later.
*/


/***************************************************************
 * 19. WHY THERE IS NO REAL WORK QUEUE
 ***************************************************************

/*
Notice:
    pdflush_list is not a queue of jobs.
    It is a list of idle workers.

This means pdflush uses direct worker assignment:
    caller hands callback directly to one sleeping worker.

No idle worker?
    → no dispatch

This keeps design simple, but also means:
    - limited buffering
    - coarser control
    - less scalable than later per-bdi flusher designs
*/


/***************************************************************
 * 20. start_one_pdflush_thread()
 ***************************************************************

/*
Function:
    static void start_one_pdflush_thread(void)

Implementation:
    kthread_run(pdflush, NULL, "pdflush");

Meaning:
    create and run one kernel thread named "pdflush"
*/


/***************************************************************
 * 21. pdflush_init()
 ***************************************************************

/*
Function:
    static int __init pdflush_init(void)

Flow:
    for i in [0 .. MIN_PDFLUSH_THREADS):
        start_one_pdflush_thread()

So system boots with minimum worker pool ready.
*/


/*
module_init(pdflush_init);

Even though this is core kernel code in tree, module_init is the standard
initialization hook macro.
*/


/***************************************************************
 * 22. HOW page-writeback.c USES pdflush
 ***************************************************************

/*
Examples from writeback side:

1. Background writeback:
    pdflush_operation(background_writeout, 0)

2. Periodic old-page writeback:
    pdflush_operation(wb_kupdate, 0)

3. Laptop mode sync:
    pdflush_operation(laptop_flush, 0)

So pdflush is generic callback executor for writeback-related jobs.
*/


/***************************************************************
 * 23. IMPORTANT CONCURRENCY IDEAS
 ***************************************************************

/*
1. pdflush_lock protects:
    - idle list
    - nr_pdflush_threads
    - last_empty_jifs related transitions
    - fn / arg publication for assignment path

2. Worker publishes itself as idle BEFORE schedule()
    so dispatcher can find it.

3. Dispatcher removes worker from idle list BEFORE wakeup
    so that worker knows it has been assigned.

4. Callback executes outside lock.

5. Growth/shrink decisions are heuristic, not exact.
*/


/***************************************************************
 * 24. WHY PF_FLUSHER MATTERS
 ***************************************************************

/*
Comment in source says kernel tries to avoid more than one pdflush thread
performing writeback against a single filesystem.

PF_FLUSHER helps other code recognize these special worker threads.
This is part of larger writeback coordination.
*/


/***************************************************************
 * 25. WHY PF_SWAPWRITE MATTERS
 ***************************************************************

/*
Writeback workers may run under memory pressure while trying to free memory.
They may need privileges/allowances to enter write paths that ordinary tasks
in reclaim-sensitive contexts cannot.

PF_SWAPWRITE is commonly used for tasks allowed to write to backing store
while system is reclaiming memory.
*/


/***************************************************************
 * 26. LIMITATIONS OF THIS DESIGN
 ***************************************************************

/*
1. Global pool
   - not per-device / per-bdi
   - coarse-grained

2. No queued jobs
   - if no idle worker, dispatch fails immediately

3. Scaling is heuristic
   - based on idle/non-idle duration
   - not based on detailed device topology

4. Less optimal for many backing devices
   - later kernels improved this a lot
*/


/***************************************************************
 * 27. INTERVIEW MENTAL MODEL
 ***************************************************************

/*
Think of pdflush as:

    "A small kernel worker-pool that runs background writeback jobs."

NOT:
    "The code that actually writes pages to disk"

The actual dirty-page policy is in page-writeback.c.
The actual disk write logic is mostly in filesystem + block layer.
*/


/***************************************************************
 * 28. END-TO-END STORY YOU SHOULD BE ABLE TO SAY
 ***************************************************************

/*
A process dirties file-backed pages.
page-writeback.c notices dirty memory crossing thresholds.
It may ask pdflush to run background_writeout().
pdflush_operation() finds an idle pdflush kernel thread and assigns
that callback. The worker wakes up and runs background_writeout(),
which eventually walks dirty inodes/pages and calls filesystem
writepage()/writepages(). Those submit BIOs to storage. When done,
the pdflush worker goes back to sleep on the idle list.

If all pdflush workers remain busy for >1 second, the pool may grow.
If workers remain idle for >1 second and count is above the minimum,
the pool may shrink.
*/


/***************************************************************
 * 29. DEBUGGING QUESTIONS
 ***************************************************************

/*
Q: Why did pdflush_operation() return -1?
A: No idle pdflush worker existed at that moment.

Q: Why are more pdflush threads appearing?
A: Pool stayed empty >1 second under load, so growth heuristic triggered.

Q: Why did one pdflush thread disappear?
A: A worker had been idle >1 second and pool was above MIN.

Q: Does pdflush itself know about filesystems or pages?
A: Not really. It mostly runs callback functions supplied by writeback code.

Q: Is pdflush a workqueue?
A: Not exactly. It is a callback-based worker pool with direct assignment,
   not a general queued work item framework.
*/


/***************************************************************
 * 30. HOW TO READ THIS FILE
 ***************************************************************

/*
Recommended order:

1. pdflush_operation()
   - understand dispatch API

2. __pdflush()
   - understand worker lifecycle

3. growth/shrink heuristics
   - understand pool management

4. pdflush()
   - understand thread setup

5. pdflush_init()
   - understand boot-time startup

Then connect it back to:
    mm/page-writeback.c
*/


/***************************************************************
 * 31. ONE-LINE SUMMARY
 ***************************************************************

/*
pdflush.c in Linux 2.6.20 implements a small dynamically sized pool of
kernel flusher threads that asynchronously execute writeback callbacks
requested by page-writeback.c.
*/


/***************************************************************
 * END
 ***************************************************************/

