# FULL DETAILED OOM-KILLER FLOW DIAGRAM (Linux 2.6.20)
Absolutely — here is a single, complete, carefully formatted, fully detailed flow diagram of the Linux 2.6 OOM-killer (oom_kill.c).
- Nothing truncated
- One cohesive diagram
- Shows interaction between:
    __alloc_pages() → OOM → out_of_memory()
    select_bad_process()
    badness()
    oom_kill_process() + oom_kill_task()
    Signal delivery + cleanup


┌────────────────────────────────────────────────────────────────────┐
│                     MEMORY ALLOCATION FAILURE                      │
│       (__alloc_pages() cannot allocate even after reclaim)         │
└────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
                   ┌──────────────────────────────────┐
                   │ out_of_memory(zonelist,gfp,order)│
                   └──────────────────────────────────┘
                                      │
                                      ▼
          ┌─────────────────────────────────────────────────────────┐
          │ 1. Notify OOM notifiers (drivers, subsystems)           │
          │    freed = blocking_notifier_call_chain()               │
          └─────────────────────────────────────────────────────────┘
                                      │
                        freed > 0 ?   │   freed == 0?
                       ───────────────┘
               YES → return                ▼
                                            │
                                            ▼
                 ┌─────────────────────────────────────────────┐
                 │ 2. Print warning, show stack, show memory   │
                 └─────────────────────────────────────────────┘
                                            │
                                            ▼
                 ┌──────────────────────────────────────────┐
                 │ 3. Lock cpusets + read_lock tasklist     │
                 └──────────────────────────────────────────┘
                                            │
                                            ▼
      ┌──────────────────────────────────────────────────────────────────┐
      │ 4. Determine allocation constraint (NUMA/cpuset/policy)          │
      └──────────────────────────────────────────────────────────────────┘
                                            │
                                            ▼
         ┌────────────────────────────────────────────────────────────┐
         │ if CONSTRAINT_MEMORY_POLICY → kill current                  │
         │ if CONSTRAINT_CPUSET        → kill current                  │
         │ if CONSTRAINT_NONE          → normal OOM selection process  │
         └────────────────────────────────────────────────────────────┘
                                            │
                                            ▼
                     ┌───────────────────────────────────────┐
                     │ sysctl_panic_on_oom ? panic()         │
                     └───────────────────────────────────────┘
                                            │
                                            ▼
                           ┌──────────────────────────────┐
                           │ retry: select_bad_process()  │
                           └──────────────────────────────┘
                                            │
                                            ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ select_bad_process():                                                      │
│   ● Iterate through every thread                                           │
│   ● Skip kernel threads, skip init                                         │
│   ● Skip OOM_DISABLE tasks                                                 │
│   ● If any thread has TIF_MEMDIE → return ERR_PTR(-1) (avoid deadlock)     │
│   ● Compute badness() score for each eligible process                      │
│   ● Choose task with highest score                                         │
└────────────────────────────────────────────────────────────────────────────┘
                                            │
                                            ▼
                ┌──────────────────────────────────────────┐
                │ No killable task? → panic()              │
                └──────────────────────────────────────────┘
                                            │
                                            ▼
      ┌──────────────────────────────────────────────────────────────────┐
      │5. Attempt to kill chosen process: oom_kill_process(p,score,msg) │
      └──────────────────────────────────────────────────────────────────┘
                             │
                             ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ oom_kill_process():                                                        │
│   ● If p->flags & PF_EXITING → just set TIF_MEMDIE and return              │
│   ● print "Killed process PID (comm)"                                      │
│   ● Try killing a child first (if the child has independent mm)            │
│     - if successful → return                                               │
│   ● else kill the parent process                                           │
└────────────────────────────────────────────────────────────────────────────┘
                             │
                             ▼
                 ┌──────────────────────────────────────────────┐
                 │ 6. oom_kill_task(p)                          │
                 └──────────────────────────────────────────────┘
                             │
                             ▼
┌────────────────────────────────────────────────────────────────────────────┐
│ oom_kill_task():                                                           │
│   ● If mm == NULL → nothing to kill                                        │
│   ● If any thread of mm is OOM_DISABLE → skip killing                      │
│   ● __oom_kill_task(p, verbose)                                            │
│        - give it a full time slice (HZ)                                    │
│        - set TIF_MEMDIE so it can use reserves                             │
│        - send SIGKILL                                                      │
│   ● Kill all threads sharing the mm (other thread groups)                   │
└────────────────────────────────────────────────────────────────────────────┘
                             │
                             ▼
             ┌─────────────────────────────────────────────┐
             │ 7. Unlock tasklist + cpusets                │
             └─────────────────────────────────────────────┘
                             │
                             ▼
   ┌──────────────────────────────────────────────────────────────────┐
   │ 8. If caller itself is not dying (no TIF_MEMDIE), sleep 1 tick  │
   │    schedule_timeout_uninterruptible(1)                          │
   └──────────────────────────────────────────────────────────────────┘
                             │
                             ▼
              ┌──────────────────────────────────────────┐
              │ Return to allocator; try allocation again│
              └──────────────────────────────────────────┘
