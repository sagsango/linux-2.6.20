# Scheduler Data Structures Overview
 ┌────────────────────────────────────────────────────────────┐
 │                       Per-CPU scheduler                    │
 └────────────────────────────────────────────────────────────┘
        ┌─────────────────────────────────────────────────────┐
        │ struct rq  (Runqueue for one CPU)                   │
        │-----------------------------------------------------│
        │ spinlock_t lock;                                    │
        │ unsigned long nr_running;     ← # runnable tasks     │
        │ struct task_struct *curr;    ← currently running     │
        │ struct task_struct *idle;    ← idle task             │
        │ struct prio_array *active;   ← points to arrays[0]   │
        │ struct prio_array *expired;  ← points to arrays[1]   │
        │ struct prio_array arrays[2]; ← two scheduling banks  │
        │ struct sched_domain *sd;     ← SMP load balance info │
        │ struct list_head migration_queue;                    │
        └──────────────────────────────┬───────────────────────┘
                                       │
                                       │
             ┌─────────────────────────┴────────────────────────┐
             │ struct prio_array (active or expired)             │
             │--------------------------------------------------│
             │ unsigned int nr_active;                          │
             │ unsigned long bitmap[MAX_PRIO/32];               │
             │ struct list_head queue[MAX_PRIO];                │
             │    ↑ 0..139 priority lists                       │
             │    │                                             │
             │    └──────> each queue holds tasks of same prio  │
             └──────────────────────────────────────────────────┘
                                       │
                                       ▼
                         ┌───────────────────────────┐
                         │ struct task_struct        │
                         │---------------------------│
                         │ state                     │
                         │ prio / static_prio        │
                         │ policy (SCHED_*)          │
                         │ time_slice                │
                         │ sleep_avg                 │
                         │ run_list  → link node     │
                         │ array     → back pointer  │
                         │ sched_info, cpus_allowed  │
                         └───────────────────────────┘




# How tasks live inside the per-CPU runqueue
cpu_rq(X)
 ├── rq->active → [prio_array 0]
 │       ├── queue[100] →  [ task A  →  task B  ]
 │       ├── queue[101] →  [ task C ]
 │       ├── queue[120] →  [ task D  →  task E ]
 │       └── bitmap → bits set where queues nonempty
 │
 └── rq->expired → [prio_array 1]
         (will receive tasks when their quantum expires)



# When all queues in active are empty, the scheduler swaps:
temp = rq->active;
rq->active = rq->expired;
rq->expired = temp;


# schedule() → pick_next_task() → context_switch()
                ┌─────────────────────────────────────┐
                │          schedule()                 │
                └─────────────────────────────────────┘
                           │
   (1) Acquire rq->lock    ▼
   ┌───────────────────────────────────────────────┐
   │ deactivate_task(current)                      │
   │  - remove current from runqueue               │
   │  - update accounting, nr_running--            │
   └───────────────────────────────────────────────┘
                           │
   (2) pick_next_task()    ▼
   ┌───────────────────────────────────────────────┐
   │ idx = sched_find_first_bit(rq->active->bitmap)│
   │ next = first entry in queue[idx]              │
   │ if (!next) switch active↔expired arrays       │
   └───────────────────────────────────────────────┘
                           │
   (3) context_switch(prev, next, rq)
                           │
   ▼─────────────────────────────────────────────────────────────▼
   save CPU state of prev          restore state of next
   if (prev->mm != next->mm)       switch_mm(prev->mm, next->mm)
   update rq->curr = next          update timestamp, time_slice
   ▼─────────────────────────────────────────────────────────────▼
   finish_arch_switch(prev)        prepare_arch_switch(next)



# Time slice and interactivity feedback
Each tick (scheduler_tick):
   current->time_slice--

   if (current->time_slice == 0)
        if (TASK_INTERACTIVE(current))
             move to rq->active (round-robin)
        else
             move to rq->expired

   Recalculate sleep_avg:
        sleep_avg = min(sleep_avg + bonus, MAX_SLEEP_AVG)

   Update dynamic priority:
        prio = static_prio - bonus


# Priority-based queue selection
prio range: 0 .. 139
   0..99  → real-time (SCHED_FIFO/RR)
   100..139 → normal (SCHED_NORMAL/BATCH)

rq->active->bitmap example:

bitmap bits: 0000 0000 0001 0000 ...
                ↑
                bit #100 set → nonempty queue
                ▼
          queue[100]: → [T1]→[T2]

pick_next_task() picks the lowest-numbered set bit (highest priority).

# Context switching & state diagram
     ┌────────────────────────────────────────────────────────┐
     │ current->state == TASK_RUNNING                         │
     │    ↓                                                   │
     │ do_something();                                        │
     │ set_current_state(TASK_INTERRUPTIBLE);                 │
     │ schedule();                                            │
     └─────┬──────────────────────────────────────────────────┘
           │
           ▼
    scheduler dequeues it from rq.active
           │
           ▼
    pick_next_task() → chooses next
           │
           ▼
    context_switch() → load next->thread state
           │
           ▼
    (when event wakes current)
           │
           ▼
    wake_up_process(current)
       → state=TASK_RUNNING
       → enqueue_task(current, rq.active)


# Multiprocessor (SMP) Balancing
        ┌───────────────────────────────────────────────┐
        │  sched_domain (rq->sd)                        │
        ├───────────────────────────────────────────────┤
        │  parent / child pointers                      │
        │  flags: SD_LOAD_BALANCE, SD_WAKE_IDLE, ...    │
        │  struct sched_group *groups                   │
        └────────────────────────┬──────────────────────┘
                                 │
                                 ▼
        ┌───────────────────────────────────────────────┐
        │  sched_group                                 │
        ├───────────────────────────────────────────────┤
        │  cpumask_t cpumask  ← CPUs in this group      │
        │  unsigned long cpu_power                      │
        │  struct sched_group *next                     │
        └───────────────────────────────────────────────┘

On each tick, load-balancer may migrate tasks between rqs to equalize rq->nr_running based on these structures.

# Interactivity Heuristic (sleep_avg, bonus, etc.)
CURRENT_BONUS(p)
 = (p->sleep_avg / MAX_SLEEP_AVG) * MAX_BONUS

Dynamic priority:
   prio = static_prio - CURRENT_BONUS(p)

Interactive if:
   (p->prio <= p->static_prio - DELTA(p))

If interactive → reinsert into active array (not expired)
Else → move to expired array (lower chance to run soon)

Effect:
CPU-bound (hog) task → lower sleep_avg → lower bonus → higher prio number
I/O-bound (interactive) task → higher sleep_avg → higher bonus → runs sooner


# ASCII Summary: The Complete O(1) Scheduling Engine
                +---------------------------------------------------------+
                |                LINUX O(1) SCHEDULER (2.6.20)            |
                +---------------------------------------------------------+
                |  Per-CPU struct rq                                       |
                |   ├ active / expired prio_array                          |
                |   ├ curr / idle / nr_running                             |
                |   ├ spinlock / cpu_load                                  |
                |   └ sched_domain (SMP balancing)                         |
                |                                                         |
                |  Each prio_array:                                        |
                |   ├ queue[0..139] → linked task_struct.run_list          |
                |   └ bitmap to find next queue O(1)                       |
                |                                                         |
                |  Each task_struct:                                       |
                |   ├ prio / static_prio / policy / time_slice             |
                |   ├ sleep_avg / cpus_allowed / sched_info                |
                |   ├ run_list / array link                                |
                |   └ accounting (utime/stime/nvcsw...)                    |
                |                                                         |
                |  schedule():                                             |
                |   1. lock rq                                            |
                |   2. deactivate current                                 |
                |   3. pick_next_task() (lowest set bit)                   |
                |   4. context_switch(prev,next)                           |
                |   5. unlock rq                                           |
                |                                                         |
                |  O(1) property: constant-time enqueue/dequeue/pick       |
                |                                                         |
                +---------------------------------------------------------+
