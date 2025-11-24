# Process group vs thread group

Process Group PGID = 1234
 ├─ Process A (TGID 2000)
 │   ├─ Thread A1 (PID 2000)
 │   ├─ Thread A2 (PID 2001)
 │   └─ shared signal_struct → shared_pending_A
 │
 ├─ Process B (TGID 2010)
 │   ├─ Thread B1 (PID 2010)
 │   └─ shared signal_struct → shared_pending_B
 │
 └─ Process C (TGID 2020)
     ├─ Thread C1 (PID 2020)
     ├─ Thread C2 (PID 2021)
     ├─ Thread C3 (PID 2022)
     └─ shared signal_struct → shared_pending_C

kill(-1234, SIGTERM)
   → __kill_pgrp_info()
        → send signal to each process (A,B,C)
            → each process enqueues in its own shared_pending
            → shared among its threads






# task_struct & signal_struct & sighand_struct & sigpending & sigqueue
                           ┌─────────────────────────────┐
                           │  struct signal_struct       │   (shared by all threads)
                           │-----------------------------│
                           │ struct sigpending           │
                           │  shared_pending ────────────┼──┐
                           │                             │  │
                           │ int group_stop_count        │  │
                           │ int flags                   │  │
                           │ struct task_struct *curr_target │
                           │ ...                         │  │
                           └─────────────────────────────┘  │
                                                           │
                shared pending for whole thread group      │
                                                           ▼
                                             ┌──────────────────────┐
                                             │ struct sigpending    │
                                             │----------------------│
                                             │ sigset_t signal;     │ ← bitmap of pending signals
                                             │ struct list_head list│ ← queue of sigqueue entries
                                             └──────────────────────┘
                                                       │
                               ┌────────────────────────┴────────────────────┐
                               ▼                                             ▼
                      struct sigqueue                                struct sigqueue
                      { si_signo = SIGTERM }                         { si_signo = SIGCHLD }

struct sigqueue = one pending instance

                           ┌─────────────────────────────┐
                           │  struct sighand_struct      │   (shared by all threads)
                           │-----------------------------│
                           │ spinlock_t siglock          │ ← protects all below
                           │ struct k_sigaction action[] │ ← per-signal handlers
                           │                             │   (SIG_DFL / SIG_IGN / user fn)
                           └─────────────────────────────┘


Each thread (one per LWP)
┌────────────────────────────────────────────────────────────┐
│ struct task_struct                                         │
│------------------------------------------------------------│
│ struct sigpending pending;      ← per-thread pending queue │
│ struct signal_struct *signal;   ← shared process-wide state │
│ struct sighand_struct *sighand; ← shared handler table + lock │
│ ...                                                        │
└────────────────────────────────────────────────────────────┘
             │                           │
             │ shared among threads       │ per-thread pending
             ▼                           ▼
        same signal_struct          separate sigpending


Thread group example:

   ┌──────────────┐       ┌──────────────┐
   │ task_struct  │       │ task_struct  │
   │  (thread 1)  │       │  (thread 2)  │
   │--------------│       │--------------│
   │ pending      │       │ pending      │
   │ signal  ─────┼──────▶│              │
   │ sighand ─────┼──────▶│              │
   └──────────────┘       └──────────────┘

Both threads share:
    → one `signal_struct` (shared_pending, exit flags, etc.)
    → one `sighand_struct` (handlers and siglock)




# whom to deliver
User-space: kill(pid, sig)
     ↓
sys_kill()
     ↓
kill_something_info()
     ├── pid == 0   → kill_pg_info()  → group_send_sig_info()
     ├── pid == -1  → for_each_process() → group_send_sig_info()
     ├── pid < 0    → kill_pg_info()
     └── pid > 0    → kill_proc_info() → kill_pid_info() → group_send_sig_info()



