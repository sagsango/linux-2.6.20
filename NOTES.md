# Overview exit & wait
sys_exit
  ↓
do_exit
  ↓
exit_mm
exit_files
exit_fs
exit_thread
exit_notify
  ↓
parent SIGCHLD
  ↓
task->exit_state = ZOMBIE
  ↓
wait4()
  ↓
wait_task_zombie
  ↓
release_task
    ↓
    __exit_signal
    __unhash_process
    release_thread
    call_rcu(delayed_put_task_struct)







# exit syscall from userspace
userspace
   |
   |  exit(status)
   v
==============================
 sys_exit(int error_code)
==============================
   |
   |  // Only packs status into kernel exit_code format
   |
   |  exit_code = (error_code & 0xff) << 8
   v
---------------------------------------
 call do_exit(exit_code)
---------------------------------------







# do_exit
                    do_exit(exit_code)
                    =================
                           |
                           | 1) Basic sanity checks
                           |
                           |-- if in interrupt → panic
                           |-- if pid==0 → panic
                           |-- if exiting init → panic
                           |
                           |-- Handle ptrace(PTRACE_EVENT_EXIT)
                           |
                           v
                 Mark current as exiting
                 ------------------------
                           |
                           |  tsk->flags |= PF_EXITING
                           |
                           v
               acct_update_integrals()   (accounting)
                           |
                           v
       2) If task has mm: update RSS & VM highs
                           |
                           v
       group_dead = atomic_dec_and_test(&signal->live)
       ------------------------------------------------
       | This decrements #threads alive in the thread group.
       | If this is the *last* thread → group_dead = 1
       |
       |  If group_dead = 1:
       |      - cancel POSIX timers
       |      - exit timers
       |
       v
        acct_collect()      (billing)
                           |
                           v
       3) robust_futex_cleanup()
                           |
                           v
       audit_free(), taskstats_exit(), etc
                           |
                           v
===========================
  exit_mm(tsk)
===========================
                           |
                           v
   (see MM teardown section below)
                           |

------- After MM is gone --------
                           |
                           v
       if (group_dead)
             acct_process()    // finalize accounting
                           |
                           v
        exit_sem(tsk)
                           |
                           v
        __exit_files(tsk)  // close all FDs
                           |
                           v
        __exit_fs(tsk)     // pwd, root, namespace paths
                           |
                           v
        exit_thread()      // arch-specific: TLS, FP regs
                           |
                           v
        cpuset_exit(), exit_keys(), namespaces cleanup
                           |
                           v
        if (group_dead && leader)
             disassociate_ctty()
                           |
                           v
        module_put(binfmt->module)
                           |
                           v
        tsk->exit_code = exit_code
                           |
                           v
===========================
 proc_exit_connector()
===========================
  (sends CN event about process exit if enabled)
                           |
                           v
===========================
 exit_task_namespaces()
===========================
                           |
                           v
===========================
 exit_notify(tsk)
===========================
   This is where:
      - reparenting happens
      - SIGCHLD sent to parent
      - task becomes zombie
                           |
                           v
    tsk->exit_state = EXIT_ZOMBIE or EXIT_DEAD
                           |
                           v
===========================
  Possibly release_task()
===========================
  Only if EXIT_DEAD or parent ignores child.
                           |
                           v
---------------------------------
 Now prepare to fully die:
---------------------------------
                           |
                           v
 preempt_disable()
                           |
                           |
                           |  // Scheduler must drop this task
                           v
 tsk->state = TASK_DEAD
 ------------------------
                           |
                           v
 schedule()
 =========================
   -> task never runs again
   -> dead task is freed via RCU


# exit_mm
exit_mm(tsk)
============
     |
     v
 mm_release(tsk, mm)    // wake up waiters of exec, etc
     |
     v
 if (!mm) return;
     |
     v
 down_read(&mm->mmap_sem)
     |
     | if core dump in progress:
     |       wait for coredump synchronization
     |
     v
 task_lock(tsk)
 tsk->mm = NULL       // task is now "lazy TLB" kernel thread
 task_unlock(tsk)
     |
     v
 enter_lazy_tlb(mm, current)
     |
     v
 mmput(mm)
 ==========
   |
   | atomic_dec(mm_count)
   | if last reference:
   v
   exit_mmap(mm)
   -------------
        |
        | unmap_vmas(mm)
        | free_pgtables(mm)
        |
        v
   free mm_struct


# exit_notify
exit_notify(tsk)
================
     |
     v
 forget_original_parent(tsk)
     |
     |-- move all children to init
     |-- handle ptraced children
     |
     v
 If parent in different pgrp and jobs stopped:
       send SIGHUP + SIGCONT
     |
     v
 Determine exit_signal (SIGCHLD normally)
     |
     v
 Send notification:
     do_notify_parent(tsk, exit_signal)
     |
     v
 if exit_signal == -1 → self reaping → EXIT_DEAD
 else → EXIT_ZOMBIE


# task states
RUNNING
   |
   | sys_exit()
   v
EXITING            <-- PF_EXITING set
   |
   | do_exit()
   v
EXIT_ZOMBIE        <-- after exit_notify()
   |
   | parent calls wait4()
   v
EXIT_DEAD          <-- wait_task_zombie sets EXIT_DEAD
   |
   v
release_task()
   |
   v
free via RCU
   |
   v
put_task_struct()   <-- actually releases stack + task_struct


# summary what exit does:
1. Turns thread into PF_EXITING
2. Tears down mm, files, fs, semaphores, timers, namespace
3. Notifies parent (SIGCHLD)
4. Converts task → EXIT_ZOMBIE
5. Scheduler stops running the task
6. Parent's wait4 finally reaps the task
7. Memory finally freed through RCU
