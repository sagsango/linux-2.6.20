# Core Data Structures — Big Picture
 ┌────────────────────────────────────────────────────────────┐
 │                   PROCESS / THREAD MODEL                   │
 └────────────────────────────────────────────────────────────┘
        ┌────────────────────────────┐
        │ struct task_struct         │
        │----------------------------│
        │  ↳ one per task (thread)   │
        │  ↳ holds all scheduling,   │
        │    timing, and CPU state   │
        └────────────┬───────────────┘
                     │
                     ▼
        ┌────────────────────────────┐
        │ struct rq (runqueue)       │  ← one per CPU
        │----------------------------│
        │ holds all runnable tasks   │
        │ organizes them in          │
        │ two prio_arrays:           │
        │   active / expired         │
        └────────────┬───────────────┘
                     │
                     ▼
        ┌────────────────────────────┐
        │ struct prio_array          │
        │----------------------------│
        │ queue[MAX_PRIO] lists of   │
        │   runnable tasks per prio  │
        │ bitmap → O(1) lookup       │
        └────────────────────────────┘



# task_struct (scheduling-related members only)
 struct task_struct
 ├─ volatile long state;          // TASK_RUNNING / INTERRUPTIBLE / etc
 │                                // whether sched() may pick it
 │
 ├─ int prio;                     // dynamic priority
 ├─ int static_prio;              // base priority (nice value)
 ├─ int normal_prio;              // adjusted for policy
 ├─ unsigned long rt_priority;    // RT priority (SCHED_RR/FIFO)
 │
 ├─ unsigned long policy;         // SCHED_NORMAL / RR / FIFO / BATCH
 │
 ├─ int load_weight;              // load balancing weight
 ├─ unsigned int time_slice;      // quantum remaining
 ├─ unsigned long sleep_avg;      // avg sleep time for interactivity
 │
 ├─ unsigned long long timestamp; // last enqueue timestamp
 ├─ unsigned long long last_ran;  // last run timestamp
 ├─ unsigned long long sched_time;// total sched_clock runtime
 │
 ├─ cpumask_t cpus_allowed;       // CPU affinity mask
 │
 ├─ struct list_head run_list;    // linked list node in rq->active queue
 ├─ struct prio_array *array;     // back pointer to the prio_array
 │
 ├─ struct sched_info sched_info; // statistics (delay, cpu_time, ...)
 │
 ├─ enum sleep_type sleep_type;   // interactive / noninteractive
 │
 ├─ unsigned long nvcsw;          // voluntary context switches
 ├─ unsigned long nivcsw;         // involuntary context switches
 │
 └─ other (mm, signal, etc...)    // not directly scheduling-related




# Relation Between Priorities
 user nice: [-20 .. +19]
       │
 NICE_TO_PRIO(n) → static_prio = MAX_RT_PRIO + nice + 20
       │
       ▼
 dynamic prio = static_prio - bonus(sleep_avg)


lower prio → higher priority
higher sleep_avg → more bonus → better (smaller) prio


# Per-CPU Runqueue (struct rq)
 struct rq {
     spinlock_t lock;                  // protect runqueue access
     unsigned long nr_running;         // count of runnable tasks
     unsigned long long nr_switches;   // # of context switches
     struct task_struct *curr;         // currently running task
     struct task_struct *idle;         // per-cpu idle task
     struct prio_array *active;        // currently dispatching tasks
     struct prio_array *expired;       // tasks with expired timeslice
     struct prio_array arrays[2];      // storage for both
     int best_expired_prio;            // optimization hint
 #ifdef CONFIG_SMP
     struct sched_domain *sd;          // load-balance domain
     int cpu;                          // CPU id
 #endif
 };

Each CPU has its own runqueue:
 cpu_rq(cpu) → &per_cpu(runqueues, cpu)

# struct prio_array — priority queues
 struct prio_array {
     unsigned int nr_active;           // total runnable tasks
     unsigned long bitmap[MAX_PRIO/32];
     struct list_head queue[MAX_PRIO]; // 140 queues for priorities
 };

Each bit in bitmap corresponds to a priority level:
 bitmap[prio] = 1 → queue[prio] has at least one task


# Full hierarchy with data connections
 ┌─────────────────────────────────────────────────────────────┐
 │                  CPU 0 RUNQUEUE (rq0)                       │
 ├─────────────────────────────────────────────────────────────┤
 │ lock, nr_running, curr, idle                                │
 │                                                             │
 │ active → prio_array[0]                                      │
 │ expired → prio_array[1]                                     │
 │                                                             │
 │  active->bitmap:  ...001001000...                           │
 │      ↑ bit[100]=1 → queue[100] non-empty                    │
 │                                                             │
 │  active->queue[100]:                                        │
 │      [taskA]→[taskB]→[taskC]                                │
 │                                                             │
 │  each taskA.task_struct:                                    │
 │      .prio = 100                                            │
 │      .run_list → part of list_head inside queue[100]        │
 │      .array = active                                        │
 │      .time_slice = 100ms                                    │
 │      .sleep_avg = measured sleep (ns→jiffies)               │
 │                                                             │
 │  expired->queue[]: tasks whose time_slice exhausted          │
 └─────────────────────────────────────────────────────────────┘


# The schedule() Function Flow (Detailed)
 schedule()
 ├─ preempt_disable()
 ├─ rq = this_rq()
 ├─ spin_lock_irq(&rq->lock)
 │
 ├─ if (current->state != TASK_RUNNING)
 │      deactivate_task(current, rq)
 │
 ├─ next = pick_next_task(rq)
 │
 ├─ if (next != current)
 │      context_switch(rq, current, next)
 │
 ├─ spin_unlock_irq(&rq->lock)
 ├─ preempt_enable()
 └─ return to user/kernel


pick_next_task() internal flow:
 pick_next_task(rq)
 ├─ idx = sched_find_first_bit(rq->active->bitmap)
 │      → lowest-numbered set bit = highest priority
 ├─ queue = &rq->active->queue[idx]
 ├─ next = list_entry(queue->next, struct task_struct, run_list)
 │
 ├─ if (!next) {
 │       // active list empty → swap arrays
 │       swap(rq->active, rq->expired)
 │       goto again;
 │   }
 └─ return next

Time complexity: O(1) because sched_find_first_bit() is constant-time bitmap scan.


context_switch(prev, next, rq):
 context_switch(rq, prev, next)
 ├─ prepare_task_switch(rq, prev, next)
 │    - update rq->curr = next
 │    - update task timestamps, cpu stats
 │
 ├─ switch_mm(prev->mm, next->mm, next)
 │    - change memory map (if different process)
 │
 ├─ switch_to(prev, next, prev)
 │    - save CPU registers of prev
 │    - load registers of next
 │    - changes kernel stack pointer
 │
 ├─ finish_task_switch(rq, prev)
 │    - update accounting, clear TIF_NEED_RESCHED
 │
 └─ return

# Scheduling Tick and Timeslice Expiry
Each timer interrupt → scheduler_tick() runs:
 scheduler_tick()
 ├─ p = current
 ├─ p->time_slice--
 ├─ if (p->time_slice == 0)
 │      if (TASK_INTERACTIVE(p))
 │          requeue_task(p, rq->active)
 │      else
 │          move_task_to_expired(p, rq->expired)
 │      set_tsk_need_resched(p)
 └─ update_process_times(user)


# Interactivity Heuristic (sleep_avg → bonus → dynamic prio)
 sleep_avg = ns the task spent sleeping recently
 bonus = (sleep_avg / MAX_SLEEP_AVG) * MAX_BONUS
 dynamic_prio = static_prio - bonus


         ┌───────────────────────────┐
         │ CPU-hog task             │
         │ sleep_avg = small        │
         │ bonus = small            │
         │ prio = static_prio - 0   │
         └────────────┬─────────────┘
                      │
                      ▼
         ┌───────────────────────────┐
         │ Interactive task          │
         │ sleep_avg = large         │
         │ bonus = large             │
         │ prio = static_prio - n    │
         └───────────────────────────┘

This feedback loop ensures smooth desktop interactivity.


# Relationship Between Policy, Priority, and Timeslice
 SCHED_NORMAL / SCHED_BATCH:
    dynamic prio = static_prio ± bonus
    timeslice = [800ms ... 5ms] based on nice

 SCHED_RR / SCHED_FIFO:
    rt_priority = 0..99
    always in highest queues (0..99)
    round-robin with equal prio tasks

task_timeslice() uses static_prio_timeslice() to map priority to quantum length.


# Complete Execution Flow (ASCII Timeline)
   ┌──────────────────────────────────────────────────────────────┐
   │                        SCHEDULING FLOW                       │
   └──────────────────────────────────────────────────────────────┘

   process creation (fork)
       │
       ▼
   sched_fork()
       ├─ init prio, sleep_avg, time_slice
       ├─ enqueue_task(p, rq->active)
       └─ wake_up_new_task()

   ──────────────────────────────────────────────────────────────► time

   running task
       │
       ▼
   scheduler_tick()
       ├─ time_slice--
       ├─ if expired → move to expired array
       └─ set TIF_NEED_RESCHED flag
       │
       ▼
   interrupt exit → checks need_resched()
       │
       ▼
   schedule()
       ├─ lock rq
       ├─ pick_next_task()
       ├─ context_switch()
       └─ unlock rq
       │
       ▼
   next process runs


# SMP Balancing (Load Migration)
 ┌──────────────────────────────────────────────────────────────┐
 │                    MULTI-CPU BALANCING FLOW                  │
 └──────────────────────────────────────────────────────────────┘

 Each CPU has rq->sd (sched_domain)
       │
       ▼
 load_balance()
     ├─ for_each_domain(cpu, sd)
     ├─ check rq->nr_running vs neighbor rq
     ├─ if imbalance
     │      pull highest-prio task from busy CPU rq
     │      enqueue it in current rq
     └─ update cpu_load[]


     [CPU0 rq]        [CPU1 rq]
     nr_running=5      nr_running=1
           │                │
           └────→ load_balance() → migrate one task →──┘


# Complete Summary Diagram
   +--------------------------------------------------------------------+
   |                  LINUX 2.6.20 SCHEDULER (O(1))                     |
   +--------------------------------------------------------------------+
   |                                                                    |
   |  Per-task: struct task_struct                                      |
   |   ├ state, prio, static_prio, rt_priority                          |
   |   ├ policy, time_slice, sleep_avg                                  |
   |   ├ run_list, array, cpus_allowed                                  |
   |   ├ timestamps, sched_info, nvcsw/nivcsw                           |
   |                                                                    |
   |  Per-CPU: struct rq                                                |
   |   ├ curr, idle, active, expired, nr_running                        |
   |   ├ arrays[2]: struct prio_array                                   |
   |   └ lock, load info, sched_domain                                  |
   |                                                                    |
   |  schedule():                                                       |
   |   1. deactivate current                                            |
   |   2. pick_next_task()  → bitmap lookup                             |
   |   3. context_switch(prev, next)                                    |
   |                                                                    |
   |  O(1) behavior: constant time enqueue/dequeue/pick                 |
   |                                                                    |
   |  SMP load_balance(): migrates tasks between rq’s                   |
   |                                                                    |
   +--------------------------------------------------------------------+




