# We need to track what resource get freed when 
CHILD CALLS exit()
───────────────────────────────────────────
do_exit()
  ├── exit_mm()
  │      ├── mmput(mm)
  │      │     ├── exit_mmap(mm)
  │      │     │     ├── unmap VMAs
  │      │     │     ├── free user pages
  │      │     │     └── free page tables (PGD/PMD/PTE)
  │      │     └── (mm_count still > 0)
  │      └── tsk->mm = NULL
  ├── exit_notify()
  │      └── p->exit_state = EXIT_ZOMBIE
  └── schedule()   ← zombie sleeps forever

ZOMBIE EXISTS WITH:
  • task_struct still allocated
  • mm_struct still allocated (but empty)
  • page tables already destroyed


PARENT CALLS wait4()
───────────────────────────────────────────
wait4()
 └── wait_task_zombie()
       ├── xchg(state → EXIT_DEAD)
       ├── copy exit code & rusage
       └── release_task(child)


RELEASE TASK
───────────────────────────────────────────
release_task()
   ├── __exit_signal()
   ├── release_thread()
   └── call_rcu(delayed_put_task_struct)


RCU CALLBACK
───────────────────────────────────────────
delayed_put_task_struct()
   └── put_task_struct()
         └── free_task()
              ├── mmdrop(mm)
              │     └── free_mm(mm)   ← mm_struct finally freed
              └── kfree(task_struct)  ← task_struct freed

───────────────────────────────────────────
  ✓ At this point EVERYTHING is freed:
      - user pages
      - page tables
      - VMAs
      - mm_struct
      - task_struct
───────────────────────────────────────────


# exit & wait
All this gets freed inside do_exit():
    mm_struct (via exit_mm → mmput → exit_mmap)
    page tables
    mapping structures
    files_struct (fd table)
    fs_struct
    semaphores
    cpusets
    namespaces
    timers
    robust lists
    tty
    ptrace
BUT NOT:
    task_struct
    thread_info
    kernel stack
    These remain until the final RCU callback.


# all resources freeing
     do_exit()
         |
         |  frees:
         |     mm
         |     vmas
         |     pagetables
         |     files
         |     fs
         |     semaphores
         |     timers
         |     namespaces
         |
         v
   task->state = TASK_DEAD
         |
         v
     schedule()
         |
         v
  finish_task_switch()
         |
         | drop final ref
         v
  release_task()   (if parent waited)
         |
         v
  call_rcu(delayed_put_task_struct)
         |
         | RCU GRACE PERIOD
         |
         v
  delayed_put_task_struct()
         |
         v
  free kernel stack + task_struct     <-- (THIS IS POINT 7)


