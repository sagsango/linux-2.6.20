# thrash.c
When Linux is low on memory and swapping heavily:
Many tasks fault pages rapidly.
All tasks compete for swap-in bandwidth.
No task makes progress → global thrashing.
Without protection:
Tasks: T1, T2, T3, T4
         │   │   │   │
Page Faults → all constantly → disk thrash
Goal: Give one task at a time special access so it can complete a chunk of work → then pass the token to someone else.

# THE KEY STRUCTURES
global_faults       -- global page fault counter (monotonically increasing)
swap_token_mm       -- the mm_struct that currently holds the token
swap_token_lock     -- spinlock protecting the token

mm_struct fields:
    faultstamp      -- global_faults value when this mm last faulted
    last_interval   -- previous interval between faults
    token_priority  -- dynamic priority for winning the token


# WHEN IS IT CALLED?
handle_mm_fault()
      │
      └──→ do_swap_page()
               │
               └──→ grab_swap_token()


# HIGH-LEVEL TOKEN MECHANISM
=========================================================
               SWAP TOKEN ALGORITHM OVERVIEW
=========================================================

           MANY PAGE FAULTS FROM MANY TASKS
                         │
                         ▼
      Each task calls grab_swap_token() on swap fault
                         │
                         ▼
      Swap token lock gives exclusive right to one mm
                         │
                         ▼
   Token holder gets PRIORITY BOOST → fewer evictions
                         │
                         ▼
       That task runs longer & finishes its working set
                         │
                         ▼
        Token passed slowly to another contender


# FULL DETAILED FLOW DIAGRAM — grab_swap_token()
┌──────────────────────────────────────────────────────────────┐
│                     grab_swap_token()                         │
└──────────────────────────────────────────────────────────────┘

                    (Task faults a swapped-out page)
                                      │
                                      ▼
                     ┌─────────────────────────────────┐
                     │ global_faults++                 │
                     │ interval =                      │
                     │   global_faults - mm->faultstamp│
                     └─────────────────────────────────┘
                                      │
                                      ▼
                    ┌────────────────────────────────────┐
                    │ spin_trylock(&swap_token_lock)?    │
                    └────────────────────────────────────┘
                       │                     │
              lock FAIL│                     │ lock OK
                       ▼                     ▼
                  ┌──────────────┐   ┌─────────────────────────┐
                  │ return (no-op)│   │ continue algorithm      │
                  └──────────────┘   └─────────────────────────┘
                                              │
                                              ▼
                        ┌──────────────────────────────────────┐
                        │ Is swap_token_mm == NULL ?           │
                        └──────────────────────────────────────┘
                                │                    │
                                │ YES                │ NO
                                ▼                    ▼
           ┌────────────────────────────────┐   ┌──────────────────────────────┐
           │ TOKEN FREE:                    │   │ TOKEN HELD BY ANOTHER mm    │
           │   mm->token_priority += 2      │   └──────────────────────────────┘
           │   swap_token_mm = current->mm  │                │
           └────────────────────────────────┘                ▼
                                │            ┌──────────────────────────────────┐
                                │            │ Is current->mm == swap_token_mm?│
                                │            └──────────────────────────────────┘
                                │                 │                     │
                                └──────────── NO  │ YES                │ NO
                                                  ▼                     ▼
                           ┌────────────────────────────┐   ┌──────────────────────────────┐
                           │ TOKEN HOLDER RETURNS AGAIN  │   │  COMPETING FOR THE TOKEN     │
                           │   mm->token_priority += 2   │   └──────────────────────────────┘
                           └────────────────────────────┘                │
                                                                         ▼
                      ┌───────────────────────────────────────────────────────────────────┐
                      │            PRIORITY UPDATE FOR COMPETING TASK                      │
                      └───────────────────────────────────────────────────────────────────┘
                           │
                           │ interval < mm->last_interval ?
                           │
                  ┌────────┴────────┐
                  │                 │
              YES │                 │ NO
                  ▼                 ▼
        ┌────────────────┐   ┌─────────────────────────┐
        │ mm->priority++ │   │ mm->priority--          │
        └────────────────┘   │ if < 0 → set to 0       │
                             └─────────────────────────┘
                                       │
                                       ▼
          ┌──────────────────────────────────────────────────────────┐
          │ Does current->mm->token_priority > swap_token_mm->token?│
          └──────────────────────────────────────────────────────────┘
                             │                         │
                         YES │                         │ NO
                             ▼                         ▼
      ┌────────────────────────────────────────┐   ┌──────────────────────────────┐
      │ TAKE TOKEN:                            │   │ Keep old token holder        │
      │   current->mm->token_priority += 2     │   └──────────────────────────────┘
      │   swap_token_mm = current->mm          │
      └────────────────────────────────────────┘
                             │
                             ▼
         ┌─────────────────────────────────────────────────────────┐
         │ FINAL BOOKKEEPING                                       │
         │   mm->faultstamp   = global_faults                      │
         │   mm->last_interval = interval                          │
         │ spin_unlock(&swap_token_lock)                           │
         └─────────────────────────────────────────────────────────┘

                                      │
                                      ▼
                        ┌──────────────────────────────┐
                        │  RETURN (token may be held,   │
                        │          stolen, or unchanged)│
                        └──────────────────────────────┘

