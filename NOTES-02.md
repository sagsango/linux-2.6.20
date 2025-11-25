# Overall hierarchy — from CPU → runqueue → task_struct
 ┌───────────────────────────────────────────────────────────────┐
 │                        CPU (per-core)                         │
 │                                                               │
 │   ┌───────────────────────────────────────────────────────┐    │
 │   │ struct rq (Runqueue)                                 │    │
 │   │-------------------------------------------------------│    │
 │   │  struct prio_array *active;   → active tasks          │    │
 │   │  struct prio_array *expired;  → tasks whose quantum   │    │
 │   │  struct task_struct *curr;    → current running task  │    │
 │   │  spinlock_t lock;                                     │    │
 │   │  unsigned long nr_running;                            │    │
 │   │  unsigned long cpu_load;                              │    │
 │   └─────────────┬──────────────────────────────────────────┘    │
 │                 │                                               │
 │                 ▼                                               │
 │        ┌─────────────────────────────┐                          │
 │        │ struct prio_array (active)  │                          │
 │        │-----------------------------│                          │
 │        │  struct list_head queue[140]; ← one per priority       │
 │        │  unsigned long bitmap[5];     ← quick lookup            │
 │        │  unsigned int nr_active;                                │
 │        └─────────────┬───────────────────────────────────────────┘
 │                      │
 │        ┌─────────────┴──────────────────────────────────────────┐
 │        │ List of struct task_struct                             │
 │        ├────────────────────────────────────────────────────────│
 │        │  task_struct::run_list  ← links task into queue        │
 │        │  task_struct::array     ← pointer to prio_array         │
 │        │  task_struct::prio      ← dynamic priority              │
 │        │  task_struct::policy    ← SCHED_NORMAL / RR / FIFO...  │
 │        │  task_struct::time_slice ← remaining quantum            │
 │        │  task_struct::sleep_avg  ← interactivity weight         │
 │        │  task_struct::cpus_allowed ← CPU affinity mask          │
 │        │  task_struct::sched_info  ← timing stats                │
 │        │  task_struct::timestamp/last_ran ← fairness tracking    │
 │        └────────────────────────────────────────────────────────┘
 │
 └───────────────────────────────────────────────────────────────┘


# Fields inside task_struct that directly influence scheduling
 struct task_struct
 ├── state               → TASK_RUNNING / TASK_INTERRUPTIBLE / etc
 ├── prio                → dynamic priority
 ├── static_prio         → base user nice value
 ├── normal_prio         → normal scheduling value (with policy)
 ├── rt_priority         → for SCHED_RR / SCHED_FIFO
 ├── policy              → SCHED_NORMAL / FIFO / RR / BATCH
 ├── load_weight         → weight for load-balancing domain
 ├── time_slice          → quantum left
 ├── sleep_avg           → avg sleep time (interactivity)
 ├── cpus_allowed        → bitmask of allowed CPUs
 ├── array               → which prio_array it is linked into
 ├── run_list            → list node in the queue
 ├── sched_info          → accumulated runtime stats
 ├── timestamp / last_ran→ last execution timestamps
 ├── sched_time          → total CPU time (sched_clock)
 ├── nvcsw / nivcsw      → context switch counters
 └── ...


# schedule() flow — how a task is chosen and switched
 ┌─────────────────────────────────────────────────────────────┐
 │                schedule()                                   │
 └─────────────────────────────────────────────────────────────┘
                │
                │   (1) Lock current CPU runqueue
                ▼
       ┌────────────────────────────────────────────┐
       │ deactivate_task(current)                   │
       │--------------------------------------------│
       │ Removes current from rq->active->queue[prio]│
       │ Saves remaining time_slice, updates stats   │
       └────────────────────────────────────────────┘
                │
                ▼
       ┌────────────────────────────────────────────┐
       │ pick_next_task(rq)                         │
       │--------------------------------------------│
       │ Find first non-empty bit in bitmap          │
       │  → highest priority queue                   │
       │  → list_first_entry() gives next task       │
       └────────────────────────────────────────────┘
                │
                ▼
       ┌────────────────────────────────────────────┐
       │ context_switch(prev, next, rq)             │
       │--------------------------------------------│
       │  save CPU registers of prev                │
       │  switch mm_struct (if needed)              │
       │  restore thread_struct of next             │
       │  update rq->curr = next                    │
       │  start accounting timer slice              │
       └────────────────────────────────────────────┘
                │
                ▼
       ┌────────────────────────────────────────────┐
       │ update_process_times()                     │
       │ scheduler_tick()                           │
       └────────────────────────────────────────────┘




# Scheduler’s O(1) structure flow (per CPU)
 CPUx Runqueue (rq)
 ├── spinlock
 ├── *active (prio_array)
 │      ├── queue[0] → [taskA, taskB, ...]
 │      ├── queue[1] → [taskC]
 │      ├── ...
 │      └── bitmap → 1000... indicating nonempty queues
 │
 ├── *expired (prio_array)
 │      ├── holds tasks whose timeslices expired
 │
 ├── curr → pointer to currently running task
 ├── nr_running
 ├── load
 └── ...


# Active ↔ Expired swap:
time_slice expires:
    move_task_to_expired_array()
if (active->nr_active == 0)
    swap(active, expired)


# Multi-CPU balancing (sched_domain + sched_group)
        ┌───────────────────────────────────────────────┐
        │ sched_domain                                 │
        │-----------------------------------------------│
        │ struct sched_group *groups;                   │
        │ cpumask_t span;    → CPUs covered by this dom │
        │ parent / child;                               │
        │ flags: SD_LOAD_BALANCE, SD_WAKE_IDLE, ...     │
        └─────────────────┬──────────────────────────────┘
                          │
                          ▼
             ┌──────────────────────────────┐
             │ sched_group                  │
             │------------------------------│
             │ cpumask_t cpumask;           │
             │ unsigned long cpu_power;     │
             │ struct sched_group *next;    │
             └──────────────────────────────┘

Each CPU has its own sched_domain that:
Describes which neighboring CPUs it can migrate tasks to.
Balances load when one CPU is idle and others are busy.

# Time accounting and interactivity feedback
sleep_avg ─────► increases when task sleeps voluntarily
                 ↓
             higher sleep_avg → lower (better) prio
                 ↓
             scheduler treats as interactive
                 ↓
             gets CPU faster next time


timestamp / last_ran ─► used to compute how long a task waited
                         between runs (run_delay)
sched_info.run_delay ─► accumulates this
sched_info.cpu_time  ─► total runtime


# Interaction between core functions
                +---------------------+
                | wake_up_process(p)  |
                +---------+-----------+
                          |
                          ▼
                    set TASK_RUNNING
                          |
                          ▼
                  enqueue_task(p, rq)
                          |
                          ▼
                  maybe_preempt(current)
                          |
                          ▼
                if (p->prio < current->prio)
                     resched_needed


# ASCII timeline of process execution (simplified)
TIME ─────────────────────────────────────────────────────────────►

TASK_A:  [RUNNING]──┐ sleep()  ──────┐ wake_up() ───┐
                    ▼                ▼               ▼
TASK_B:             └─ [RUNNING]─────┘ [SLEEP]───────┘

Scheduler maintains:
   • state (TASK_RUNNING vs TASK_SLEEP)
   • runqueue position
   • priority (dynamic aging)
   • timestamps for fairness


# Putting it all together (macro diagram)
     +---------------------------------------------------+
     |              LINUX SCHEDULER (2.6.20)             |
     +---------------------------------------------------+
     |                                                   |
     |   struct task_struct                              |
     |   ├ state / prio / static_prio / policy           |
     |   ├ run_list / array / time_slice                 |
     |   ├ cpus_allowed / sched_info / sleep_avg         |
     |   └ utime/stime/nvcsw/nivcsw                      |
     |                                                   |
     |   ▼                                               |
     |   Enqueued in per-CPU                             |
     |   struct rq (runqueue)                            |
     |   ├ active / expired prio_arrays                  |
     |   ├ curr / nr_running / lock                      |
     |   └ load / balancing                              |
     |                                                   |
     |   ▼                                               |
     |   schedule()  picks next from rq->active          |
     |   ├ pick_next_task()                              |
     |   ├ context_switch(prev, next)                    |
     |   ├ update_process_times()                        |
     |   └ scheduler_tick()                              |
     |                                                   |
     |   ▼                                               |
     |   sched_domain  (SMP load balancing)              |
     |   └ sched_group (CPU clusters)                    |
     +---------------------------------------------------+

