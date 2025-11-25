# struct task_struct
| Category                        | Field(s)                                                                                                                            | Purpose                                                                                         |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| **Task state**                  | `volatile long state`                                                                                                               | Whether the task is runnable, sleeping, stopped, etc. (`TASK_RUNNING`, `TASK_INTERRUPTIBLE`, …) |
| **Priority & weight**           | `int prio, static_prio, normal_prio;`<br>`int load_weight;`<br>`unsigned long rt_priority;`                                         | Determines scheduling order: dynamic vs static priorities, and RT vs normal tasks               |
| **Runqueue linkage**            | `struct list_head run_list;`<br>`struct prio_array *array;`                                                                         | The position of this task in the CPU’s runqueue (O(1) scheduler)                                |
| **Scheduling policy**           | `unsigned long policy;`                                                                                                             | One of `SCHED_NORMAL`, `SCHED_RR`, `SCHED_FIFO`, `SCHED_BATCH`                                  |
| **Timeslice / time accounting** | `unsigned int time_slice, first_time_slice;`<br>`unsigned long sleep_avg;`<br>`unsigned long long timestamp, last_ran, sched_time;` | Used by fair-share or RR scheduling to measure CPU time and fairness                            |
| **Per-task scheduling info**    | `struct sched_info sched_info;` (if `CONFIG_SCHEDSTATS`)                                                                            | Keeps cumulative run delays, CPU time, etc. for stats/debug                                     |
| **Sleep classification**        | `enum sleep_type sleep_type;`                                                                                                       | Whether the task’s sleep was interactive, non-interactive, etc.                                 |
| **CPU affinity**                | `cpumask_t cpus_allowed;`                                                                                                           | Which CPUs the task may execute on                                                              |
| **Accounting**                  | `cputime_t utime, stime;`<br>`unsigned long nvcsw, nivcsw;`                                                                         | User/system time and context switch counters                                                    |
| **Interactivity heuristic**     | `unsigned long sleep_avg;`                                                                                                          | Tracks average sleep time → affects dynamic priority                                            |
| **Scheduler timestamps**        | `timestamp`, `last_ran`, `sched_time`                                                                                               | Used for load-balancing, aging, etc.                                                            |
| **IO wait hooks**               | `wait_queue_t *io_wait;`                                                                                                            | Used by `io_schedule()` to block task waiting for IO                                            |


# High-level schematic
                  ┌────────────────────────────────┐
                  │ struct task_struct             │
                  ├────────────────────────────────┤
                  │ state                          │◄── TASK_RUNNING, ...
                  │ policy (SCHED_*)               │
                  │ prio / static_prio / normal_prio│
                  │ rt_priority                    │
                  │ load_weight                    │
                  │ time_slice                     │
                  │ sleep_avg                      │
                  │ sched_info                     │
                  │ cpus_allowed                   │
                  │ run_list  ─┐                   │
                  │ array      │                   │
                  │            │  linked into CPU’s│
                  │            └──► struct rq       │
                  └────────────────────────────────┘



# Basic
long state;   // TASK_RUNNING, TASK_INTERRUPTIBLE, TASK_UNINTERRUPTIBLE...

int prio;           // current dynamic priority
int static_prio;    // base (user-set) priority
int normal_prio;    // after policy adjustments
unsigned long rt_priority; // for real-time tasks
unsigned long policy; // SCHED_NORMAL, SCHED_RR, etc.

# Flow of priority use:
user sets nice() ──► static_prio
                     │
scheduler adjusts → prio (dynamic)
                     │
runqueue uses → array[prio]

For SCHED_RR / SCHED_FIFO, rt_priority replaces nice.
For SCHED_NORMAL, dynamic priority = base ± interactivity adjustments.


# Runqueue linkages
struct task_struct {
    ...
    struct list_head run_list;
    struct prio_array *array;
    ...
}

Each CPU has a struct rq (runqueue).
The O(1) scheduler organizes all runnable tasks into 140 queues
(prio_array->queue[0..139]).
task_struct.run_list is the list node inside one of those queues.


CPU rq
 ├─ active (prio_array)
 │    ├─ queue[0] : list of highest priority tasks
 │    ├─ queue[1]
 │    └─ ...
 │
 └─ expired (prio_array)


# Time slice & interactivity
unsigned int time_slice;
unsigned long sleep_avg;
unsigned long long timestamp, last_ran;

time_slice: how long task may run before re-scheduling.
sleep_avg: running average of how long task sleeps → interactive tasks
get higher priority.
timestamp / last_ran: used to compute sleep times & fairness.



# CPU affinity and multiprocessor scheduling
cpumask_t cpus_allowed;

Each task may be bound to one or more CPUs.
The scheduler only enqueues the task on allowed CPUs.
Updated via sched_setaffinity(pid, mask).


# Scheduling info / statistics
struct sched_info sched_info;

this stores:
cpu_time, run_delay, pcnt;
last_arrival, last_queued;


# Sleep type
enum sleep_type { SLEEP_NORMAL, SLEEP_NONINTERACTIVE, ... };

Affects priority adjustment — interactive sleepers are favored.
Used by heuristics for SCHED_NORMAL.


# Context switch accounting
unsigned long nvcsw, nivcsw;


# How all these tie into the O(1) scheduler
       ┌──────────────────────────────┐
       │ struct rq (per CPU runqueue) │
       ├──────────────────────────────┤
       │ prio_array *active           │
       │ prio_array *expired          │
       │ struct task_struct *curr     │
       │ spinlock_t lock              │
       └────────────┬─────────────────┘
                    │
                    │  task_struct.run_list
                    ▼
           ┌─────────────────────┐
           │ task_struct (runnable)
           ├─────────────────────┤
           │ prio, policy, array │
           │ time_slice, cpus    │
           └─────────────────────┘

# Related kernel structures 
| Structure                 | Description                                                         |
| ------------------------- | ------------------------------------------------------------------- |
| **`struct rq`**           | Per-CPU runqueue (all runnable tasks). Holds active/expired arrays. |
| **`struct prio_array`**   | 140 lists (one per priority) + bitmap.                              |
| **`struct sched_domain`** | Multiprocessor balancing domain — connects CPUs for load balancing. |
| **`struct sched_group`**  | Group of CPUs sharing power/cache domains.                          |

# Typical code path: process sleep → wakeup
  schedule() called
     │
     ▼
  current->state = TASK_INTERRUPTIBLE
     ▼
  dequeue_task(current)
     ▼
  switch to next runnable
     ▼
  (later) wake_up_process(p)
     ▼
  p->state = TASK_RUNNING
     ▼
  enqueue_task(p)

