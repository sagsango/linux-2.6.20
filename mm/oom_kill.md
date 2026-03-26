/*
 * FILE: linux/mm/oom_kill.c  (Linux 2.6.20)
 *
 * TITLE:
 *   VERY DETAILED IDE NOTES / STUDY FILE
 *
 * GOAL:
 *   Explain the background, why this file exists, the major concepts,
 *   important data structures, decision flow, scoring logic, task selection,
 *   kill path, notifier path, NUMA/cpuset constraint path, and overall OOM
 *   control flow in Linux 2.6.20.
 *
 * STYLE:
 *   Single giant IDE-style text/source file for reading and study.
 *   Not meant to compile. Meant to be read like code notes.
 */

/*****************************************************************************/
/* 0. VERY HIGH-LEVEL SUMMARY FIRST                                           */
/*****************************************************************************/

/*
 * linux/mm/oom_kill.c is the code that decides what to do when the kernel is
 * truly out of memory and reclaim/allocation have failed badly enough that the
 * system needs an emergency response.
 *
 * In Linux 2.6.20, this emergency response is basically:
 *
 *   1. decide whether the OOM is global or constrained
 *      (NUMA policy / cpuset vs general shortage)
 *
 *   2. pick a victim task
 *
 *   3. mark it as allowed to access memory reserves (TIF_MEMDIE)
 *
 *   4. SIGKILL it (and possibly related threads)
 *
 *   5. wait a little and hope memory is released quickly enough
 *
 * This file is not general memory reclaim.
 * It is the last-resort policy engine.
 *
 * Main idea:
 *
 *   “When normal allocation failed hard, choose the least surprising and most
 *    useful process to kill so that the system can recover memory.”
 */

/*****************************************************************************/
/* 1. WHY THIS FILE EXISTS                                                    */
/*****************************************************************************/

/*
 * Normal page allocation path lives elsewhere (especially page allocator and
 * reclaim code). Usually, when memory gets tight, the kernel tries many things
 * before ever coming here:
 *
 *   - reclaim page cache
 *   - writeback dirty pages
 *   - swap out anonymous pages
 *   - direct reclaim / kswapd activity
 *   - compaction-like or zone fallback behavior depending on subsystem/version
 *
 * Only when those strategies fail enough that __alloc_pages() concludes the
 * system is truly out of memory does it invoke the OOM killer.
 *
 * So this file is the policy layer for the “we failed badly enough that some
 * task must die” moment.
 */

/*****************************************************************************/
/* 2. WHAT PROBLEM IS THIS FILE TRYING TO SOLVE?                              */
/*****************************************************************************/

/*
 * The kernel cannot just kill a random process.
 *
 * It wants a victim that ideally:
 *
 *   - frees a lot of memory
 *   - causes minimum surprise to the user/admin
 *   - avoids killing important system tasks when possible
 *   - avoids killing too many tasks
 *   - prefers processes that are more likely responsible
 *
 * So the file has two major jobs:
 *
 *   A) score tasks (“badness”)
 *   B) perform the kill in a recovery-friendly way
 */

/*****************************************************************************/
/* 3. MOST IMPORTANT GLOBALS                                                  */
/*****************************************************************************/

/*
 * int sysctl_panic_on_oom;
 *
 *   If set, global unconstrained OOM may panic the machine instead of killing
 *   a process.
 *
 * static BLOCKING_NOTIFIER_HEAD(oom_notify_list);
 *
 *   A notifier chain for subsystems that may free memory or react to OOM.
 *
 *   Before choosing a victim, out_of_memory() calls the notifier chain and
 *   gives listeners a chance to free something.
 */

/*****************************************************************************/
/* 4. THE CENTRAL CONCEPT: "BADNESS" SCORE                                   */
/*****************************************************************************/

/*
 * The most important function in the file is:
 *
 *   unsigned long badness(struct task_struct *p, unsigned long uptime)
 *
 * It computes a numeric score for a task.
 * Higher score = better victim candidate.
 *
 * The file comment is important. The algorithm wants to minimize surprise and
 * maximize usefulness.
 *
 * This is not a pure “largest RSS loses” rule.
 * It has a lot of heuristics.
 */

/*****************************************************************************/
/* 5. badness() — DETAILED FLOW                                               */
/*****************************************************************************/

/*
 * Prototype:
 *
 *   unsigned long badness(struct task_struct *p, unsigned long uptime)
 *
 * Detailed breakdown:
 *
 * -------------------------------------------------------------------------
 * STEP 1: get mm safely enough to read total_vm
 * -------------------------------------------------------------------------
 *
 *   task_lock(p)
 *   mm = p->mm
 *   if (!mm) return 0
 *   points = mm->total_vm
 *   task_unlock(p)
 *
 * Meaning:
 *   Base score starts from total virtual memory size of the process.
 *
 * Important note:
 *   They explicitly say that after unlock, local variable mm must no longer be
 *   dereferenced. The code only uses the mm pointer later for identity compare
 *   with child->mm, not for dereferencing.
 *
 * Base intuition:
 *   Bigger memory consumer should be more killable.
 *
 * -------------------------------------------------------------------------
 * STEP 2: PF_SWAPOFF gets absolute priority
 * -------------------------------------------------------------------------
 *
 *   if (p->flags & PF_SWAPOFF)
 *       return ULONG_MAX;
 *
 * Rationale:
 *   swapoff can temporarily consume tons of memory while pulling pages back in,
 *   so such a task is treated as the most kill-worthy.
 *
 * -------------------------------------------------------------------------
 * STEP 3: add contribution from children
 * -------------------------------------------------------------------------
 *
 *   list_for_each_entry(child, &p->children, sibling)
 *       if (child has different mm and child->mm exists)
 *           points += child->mm->total_vm / 2 + 1;
 *
 * Why?
 *   A forking server may spread memory pressure across many children.
 *   If we only look at one task in isolation, a parent that spawned a huge
 *   tree of workers might appear too small.
 *
 * Why only half of child memory?
 *   Because if one child is the true huge consumer, the child should still be
 *   more attractive than the parent.
 *
 * -------------------------------------------------------------------------
 * STEP 4: discount long-lived / CPU-heavy tasks
 * -------------------------------------------------------------------------
 *
 *   cpu_time = (utime + stime) converted to jiffies
 *              then shifted by (SHIFT_HZ + 3)
 *
 *   run_time = (uptime - start_time.tv_sec) >> 10
 *
 *   points /= int_sqrt(cpu_time)          if nonzero
 *   points /= int_sqrt(int_sqrt(run_time)) if nonzero
 *
 * Meaning:
 *   Old tasks or CPU-expensive tasks are often considered more valuable or more
 *   established, so their badness is reduced.
 *
 * Intuition:
 *   Killing a task that has been running long or doing substantial work may
 *   throw away more useful work.
 *
 * -------------------------------------------------------------------------
 * STEP 5: nice > 0 tasks are easier to kill
 * -------------------------------------------------------------------------
 *
 *   if (task_nice(p) > 0)
 *       points *= 2;
 *
 * Rationale:
 *   If a task is niced, user/admin already indicated it is lower priority.
 *   So make it more killable.
 *
 * -------------------------------------------------------------------------
 * STEP 6: protect more privileged / important tasks somewhat
 * -------------------------------------------------------------------------
 *
 *   if CAP_SYS_ADMIN or uid==0 or euid==0
 *       points /= 4;
 *
 * Superuser/admin processes are treated as more important.
 *
 * -------------------------------------------------------------------------
 * STEP 7: protect raw hardware access tasks
 * -------------------------------------------------------------------------
 *
 *   if CAP_SYS_RAWIO
 *       points /= 4;
 *
 * Why?
 *   Killing a task directly controlling hardware may be dangerous or surprising.
 *
 * -------------------------------------------------------------------------
 * STEP 8: cpuset overlap heuristic
 * -------------------------------------------------------------------------
 *
 *   if (!cpuset_excl_nodes_overlap(p))
 *       points /= 8;
 *
 * Meaning:
 *   If this task's cpuset nodes do not overlap ours, killing it is less likely
 *   to directly help, though maybe still somewhat helpful because the task may
 *   own memory on this node from earlier.
 *
 * -------------------------------------------------------------------------
 * STEP 9: oomkilladj adjustment
 * -------------------------------------------------------------------------
 *
 *   if (p->oomkilladj) {
 *       if positive  -> points <<= oomkilladj
 *       if negative  -> points >>= -oomkilladj
 *   }
 *
 * This is admin/user tuning.
 *
 * Very important special case elsewhere:
 *   OOM_DISABLE means task should be skipped entirely.
 *
 * Final result:
 *   return points
 */

/*****************************************************************************/
/* 6. WHAT DOES badness() REALLY MEAN CONCEPTUALLY?                           */
/*****************************************************************************/

/*
 * A process becomes a better victim if:
 *
 *   - it uses lots of memory
 *   - it has many memory-using children
 *   - it is low priority (nice > 0)
 *   - admin explicitly raised oomkilladj
 *
 * A process becomes a worse victim if:
 *
 *   - it is old / long-running / CPU-used
 *   - it is root/admin-like
 *   - it has CAP_SYS_RAWIO
 *   - it does not overlap the relevant cpuset nodes
 *   - admin explicitly lowered oomkilladj
 */

/*****************************************************************************/
/* 7. CONSTRAINT TYPES                                                        */
/*****************************************************************************/

/*
 * The file distinguishes three types of OOM context:
 *
 *   CONSTRAINT_NONE
 *   CONSTRAINT_MEMORY_POLICY
 *   CONSTRAINT_CPUSET
 *
 * Why this matters:
 *
 *   Not every allocation failure means the whole machine is out of memory.
 *   Sometimes the failure happens only because allocation is restricted by:
 *
 *     - MPOL_BIND or NUMA memory policy
 *     - cpuset constraints
 *
 * In those cases, the current task itself may be the most relevant victim,
 * rather than running the global “find worst task in system” algorithm.
 */

/*****************************************************************************/
/* 8. constrained_alloc()                                                     */
/*****************************************************************************/

/*
 * Function:
 *
 *   static inline int constrained_alloc(struct zonelist *zonelist,
 *                                       gfp_t gfp_mask)
 *
 * Purpose:
 *   Determine whether the failed allocation was globally unconstrained, or was
 *   constrained by NUMA policy/cpuset.
 *
 * Logic under CONFIG_NUMA:
 *
 *   1. Build a nodemask of nodes that have present memory.
 *   2. Walk the zones in the given zonelist.
 *   3. If cpuset softwall rejects a zone, then allocation is CPUSET constrained.
 *   4. Otherwise remove allowed nodes from the mask.
 *   5. If some nodes with memory remain outside the zonelist, then allocation
 *      was constrained by memory policy.
 *   6. Else CONSTRAINT_NONE.
 *
 * Intuition:
 *   - CPUSET constraint: zone access blocked by cpuset boundaries.
 *   - MEMORY_POLICY constraint: zonelist is narrower than all available memory.
 *   - NONE: allocation failure is truly general.
 */

/*****************************************************************************/
/* 9. select_bad_process() — GLOBAL VICTIM SELECTION                          */
/*****************************************************************************/

/*
 * Prototype:
 *
 *   static struct task_struct *select_bad_process(unsigned long *ppoints)
 *
 * Purpose:
 *   Scan the whole task list and choose the task with the highest badness.
 *
 * Important assumptions:
 *   - caller holds tasklist_lock for read
 *
 * Full flow:
 *
 *   *ppoints = 0
 *   uptime = monotonic time
 *
 *   for every thread in the system:
 *
 *     1. skip if !p->mm
 *        -> kernel threads / mm-less tasks not useful victims
 *
 *     2. skip init
 *        -> never kill init
 *
 *     3. if TIF_MEMDIE already set on some task:
 *           return ERR_PTR(-1UL)
 *
 *        Meaning:
 *           some victim is already dying with access to reserves;
 *           do not start another victim yet.
 *
 *     4. if task is PF_EXITING:
 *           if not current -> return ERR_PTR(-1UL)
 *           if current -> choose current with ULONG_MAX score
 *
 *        Meaning:
 *           if another task is already in exit path, wait for it to release
 *           memory instead of killing more tasks.
 *
 *           But if current itself is exiting, let it become the victim so it
 *           can gain reserve access and finish exiting, avoiding deadlock.
 *
 *     5. if oomkilladj == OOM_DISABLE:
 *           skip task
 *
 *     6. compute points = badness(p, uptime)
 *        choose highest-scoring process
 *
 * Returns:
 *   - chosen task
 *   - ERR_PTR(-1UL) meaning “someone already dying/exiting; back off”
 *   - NULL if no killable processes
 */

/*****************************************************************************/
/* 10. IMPORTANT SPECIAL CASES IN select_bad_process()                        */
/*****************************************************************************/

/*
 * A) TIF_MEMDIE found
 *    ----------------
 *    Means we already gave somebody access to reserves and SIGKILL.
 *    The allocator should wait rather than stampede-kill more tasks.
 *
 * B) PF_EXITING found
 *    ----------------
 *    A process in exit path may already be freeing memory.
 *    Avoid picking another task immediately.
 *
 * C) current is PF_EXITING
 *    ---------------------
 *    Then choose current and mark it MEMDIE so it can complete exit.
 */

/*****************************************************************************/
/* 11. __oom_kill_task() — THE LOW-LEVEL KILL ACTION                          */
/*****************************************************************************/

/*
 * Prototype:
 *
 *   static void __oom_kill_task(struct task_struct *p, int verbose)
 *
 * Purpose:
 *   Actually mark task as OOM victim and send SIGKILL.
 *
 * Safety checks:
 *   - refuse to kill init
 *   - refuse to kill mm-less task
 *
 * Actions:
 *   - optional printk
 *   - set p->time_slice = HZ
 *   - set_tsk_thread_flag(p, TIF_MEMDIE)
 *   - force_sig(SIGKILL, p)
 *
 * Why TIF_MEMDIE matters:
 *   This is one of the most important mechanics.
 *
 *   The chosen victim gets privileged access to memory reserves so it can
 *   continue through exit and actually release memory, instead of being stuck
 *   needing memory in order to die.
 *
 * Why increase time_slice?
 *   Give it a better chance to run immediately and free resources quickly.
 */

/*****************************************************************************/
/* 12. oom_kill_task() — MM/THREAD-GROUP AWARE KILL                           */
/*****************************************************************************/

/*
 * Prototype:
 *
 *   static int oom_kill_task(struct task_struct *p)
 *
 * Purpose:
 *   Apply policy around killing p and related tasks that share its mm.
 *
 * Flow:
 *
 *   mm = p->mm
 *   if mm == NULL -> return 1
 *
 *   Scan all threads:
 *       if q->mm == mm && p->oomkilladj == OOM_DISABLE
 *           return 1
 *
 *   __oom_kill_task(p, 1)
 *
 *   Scan all threads again:
 *       if q->mm == mm && q->tgid != p->tgid
 *           force_sig(SIGKILL, p)   [NOTE: source appears to intend q]
 *
 * Return:
 *   0 on success path
 *   1 if kill not done / should not be done
 *
 * Conceptually:
 *   - first kill chosen task robustly
 *   - then kill other processes sharing the same mm but not necessarily allow
 *     them reserve access
 *
 * Important conceptual point:
 *   Memory may be shared across thread groups if they share the same mm.
 *   Killing only one such task might not fully solve the pressure.
 */

/*****************************************************************************/
/* 13. NOTE ABOUT THE SOURCE QUIRK IN oom_kill_task()                         */
/*****************************************************************************/

/*
 * In the second loop, the source shown uses:
 *
 *     force_sig(SIGKILL, p);
 *
 * inside a loop over q.
 *
 * Logically you would expect it to signal q, not p.
 *
 * When studying old code, keep an eye out for these oddities. For conceptual
 * understanding, the intent is clearly “kill other tasks sharing the same mm”.
 */

/*****************************************************************************/
/* 14. oom_kill_process() — CHILD-FIRST POLICY                                */
/*****************************************************************************/

/*
 * Prototype:
 *
 *   static int oom_kill_process(struct task_struct *p,
 *                               unsigned long points,
 *                               const char *message)
 *
 * Purpose:
 *   Kill the chosen process, but first prefer killing a child.
 *
 * Why?
 *   Sometimes killing a child is enough and may be more directly responsible
 *   for memory pressure.
 *
 * Flow:
 *
 *   if p is PF_EXITING:
 *       just call __oom_kill_task(p, 0)
 *       return 0
 *
 *   printk(message, pid, comm, score)
 *
 *   iterate over p->children:
 *       if child shares same mm as parent -> skip
 *       else try oom_kill_task(child)
 *       if success -> return 0
 *
 *   if no child kill succeeds:
 *       return oom_kill_task(p)
 *
 * Important conceptual meaning:
 *   “kill process X or a child” is the actual policy, not always just X.
 */

/*****************************************************************************/
/* 15. OOM NOTIFIER CHAIN                                                     */
/*****************************************************************************/

/*
 * register_oom_notifier()
 * unregister_oom_notifier()
 *
 * The OOM path first calls:
 *
 *   blocking_notifier_call_chain(&oom_notify_list, 0, &freed)
 *
 * and if freed > 0, it returns without killing anything.
 *
 * Meaning:
 *   Other subsystems can participate in OOM recovery by releasing memory.
 *
 * This is a very important architectural point:
 *   OOM-kill is not necessarily the first action even after entering this file.
 *   The kernel offers one last cooperative escape hatch.
 */

/*****************************************************************************/
/* 16. out_of_memory() — TOP-LEVEL ENTRY                                      */
/*****************************************************************************/

/*
 * Prototype:
 *
 *   void out_of_memory(struct zonelist *zonelist, gfp_t gfp_mask, int order)
 *
 * This is the central entry invoked when allocator decides OOM handling is
 * necessary.
 *
 * Full flow:
 *
 * -------------------------------------------------------------------------
 * STEP 1: notifier chain chance
 * -------------------------------------------------------------------------
 *
 *   freed = 0
 *   blocking_notifier_call_chain(..., &freed)
 *   if (freed > 0)
 *       return
 *
 * So if some listener freed memory in the last second, do not kill.
 *
 * -------------------------------------------------------------------------
 * STEP 2: ratelimited diagnostics
 * -------------------------------------------------------------------------
 *
 *   printk current->comm, gfp_mask, order, current->oomkilladj
 *   dump_stack()
 *   show_mem()
 *
 * This is crucial for debugging OOM events.
 *
 * -------------------------------------------------------------------------
 * STEP 3: acquire global locks
 * -------------------------------------------------------------------------
 *
 *   cpuset_lock()
 *   read_lock(&tasklist_lock)
 *
 * -------------------------------------------------------------------------
 * STEP 4: determine whether allocation was constrained
 * -------------------------------------------------------------------------
 *
 *   switch (constrained_alloc(zonelist, gfp_mask)) {
 *
 *     case CONSTRAINT_MEMORY_POLICY:
 *         oom_kill_process(current, points,
 *                          "No available memory (MPOL_BIND)")
 *
 *     case CONSTRAINT_CPUSET:
 *         oom_kill_process(current, points,
 *                          "No available memory in cpuset")
 *
 *     case CONSTRAINT_NONE:
 *         if panic_on_oom -> panic
 *         else select global victim
 *   }
 *
 * Important:
 *   For constrained failures, current is usually blamed.
 *
 * -------------------------------------------------------------------------
 * STEP 5: global victim selection path
 * -------------------------------------------------------------------------
 *
 * retry:
 *   p = select_bad_process(&points)
 *
 *   if ERR_PTR(-1UL):
 *       goto out      // someone already dying/exiting
 *
 *   if !p:
 *       panic("Out of memory and no killable processes...")
 *
 *   if oom_kill_process(p, points, "Out of memory")
 *       goto retry
 *
 * Why retry?
 *   Because chosen task may not be killable for some policy reason, or the
 *   child-first path may fail, so we re-run selection.
 *
 * -------------------------------------------------------------------------
 * STEP 6: unlock
 * -------------------------------------------------------------------------
 *
 *   read_unlock(tasklist_lock)
 *   cpuset_unlock()
 *
 * -------------------------------------------------------------------------
 * STEP 7: yield time if current is not already MEMDIE
 * -------------------------------------------------------------------------
 *
 *   if (!test_thread_flag(TIF_MEMDIE))
 *       schedule_timeout_uninterruptible(1)
 *
 * Meaning:
 *   Give the victim a chance to run and free memory before allocation path
 *   retries aggressively.
 */

/*****************************************************************************/
/* 17. WHY CURRENT IS KILLED FOR MPOL_BIND / CPUSET CASES                     */
/*****************************************************************************/

/*
 * Suppose memory exists elsewhere in the machine, but current is restricted to
 * a subset of nodes by policy or cpuset and cannot allocate there.
 *
 * Then the problem is not “the whole machine is out of memory.”
 * It is “this task cannot allocate within its allowed region.”
 *
 * In that case, globally killing some unrelated process may be the wrong
 * policy, and the code instead blames current.
 */

/*****************************************************************************/
/* 18. WHY TIF_MEMDIE EXISTS                                                  */
/*****************************************************************************/

/*
 * This flag is one of the most important OOM concepts.
 *
 * Problem:
 *   A task may need memory in order to exit cleanly and release memory.
 *   If we SIGKILL it but deny all allocations, it could deadlock while dying.
 *
 * Solution:
 *   mark victim with TIF_MEMDIE so allocator paths can let it access memory
 *   reserves and complete exit.
 *
 * So the OOM killer is not just “send SIGKILL”; it is “send SIGKILL and give
 * the victim a privileged escape path to free memory.”
 */

/*****************************************************************************/
/* 19. WHY THE FILE WAITS FOR EXISTING EXITING/MEMDIE TASKS                   */
/*****************************************************************************/

/*
 * Without this behavior, multiple OOM events could kill many tasks in quick
 * succession even though one already-dying victim might have been sufficient.
 *
 * So select_bad_process() tries hard to detect:
 *
 *   - somebody already dying with reserve access
 *   - somebody already exiting and likely to free memory
 *
 * and in those cases it backs off.
 */

/*****************************************************************************/
/* 20. IMPORTANT LOCKING / SAFETY NOTES                                       */
/*****************************************************************************/

/*
 * badness():
 *   - task_lock(p) used briefly to read p->mm and mm->total_vm
 *   - then unlock; mm pointer must not be dereferenced afterward
 *
 * select_bad_process():
 *   - caller holds tasklist_lock read
 *
 * out_of_memory():
 *   - cpuset_lock + tasklist_lock read around core selection logic
 *
 * There are several comments warning that mm pointers may change because the
 * code is intentionally lightweight and relies on cautious usage patterns.
 */

/*****************************************************************************/
/* 21. WHAT IS THE REAL VICTIM UNIT?                                          */
/*****************************************************************************/

/*
 * Not always just one thread.
 *
 * The code often reasons in terms of mm ownership.
 *
 * Why?
 *   Memory is attached to mm_struct, not just a thread identity.
 *
 * Therefore:
 *   - selection scans tasks
 *   - but kill logic cares about shared mm too
 *   - child-first policy may redirect kill to a child
 */

/*****************************************************************************/
/* 22. HOW TO READ THE SCORING RULE QUICKLY                                   */
/*****************************************************************************/

/*
 * Approximate simplified mental formula:
 *
 *   badness ≈ memory footprint
 *              + half of child memory footprints
 *              then scaled down if old/important/privileged
 *              then scaled up if niced or oomkilladj says so
 *
 * Special override:
 *   PF_SWAPOFF => ULONG_MAX
 */

/*****************************************************************************/
/* 23. WHAT DOES THE FILE NOT DO?                                             */
/*****************************************************************************/

/*
 * It does NOT reclaim memory itself.
 * It does NOT choose pages to free.
 * It does NOT swap pages.
 * It does NOT perform allocator fallback.
 *
 * It decides which task should die when the rest of MM failed to solve the
 * shortage.
 */

/*****************************************************************************/
/* 24. DEBUG / DIAGNOSTIC OUTPUT                                              */
/*****************************************************************************/

/*
 * Under ratelimit, out_of_memory() prints:
 *
 *   - current task name
 *   - gfp_mask
 *   - order
 *   - current->oomkilladj
 *   - stack trace
 *   - show_mem() summary
 *
 * This is very useful when studying allocator failures:
 *
 *   gfp_mask tells context / allocation constraints
 *   order    tells how large contiguous allocation was requested
 */

/*****************************************************************************/
/* 25. END-TO-END FLOW                                                        */
/*****************************************************************************/

/*
 * FULL OOM FLOW (SIMPLIFIED)
 * --------------------------
 *
 * __alloc_pages() / allocator path fails badly
 *   -> out_of_memory(zonelist, gfp_mask, order)
 *       |
 *       +--> call OOM notifier chain
 *       |      if memory freed: return
 *       |
 *       +--> print diagnostics
 *       +--> lock cpuset + tasklist
 *       +--> determine constraint type
 *              |
 *              +--> MEMORY_POLICY -> kill current
 *              +--> CPUSET        -> kill current
 *              +--> NONE          -> global scan
 *                                      |
 *                                      +--> select_bad_process()
 *                                      |      - skip kernel threads/init
 *                                      |      - skip OOM_DISABLE
 *                                      |      - back off if MEMDIE/exiting
 *                                      |      - use badness() score
 *                                      |
 *                                      +--> oom_kill_process(victim)
 *                                             - maybe kill child first
 *                                             - else kill victim
 *                                             - set TIF_MEMDIE
 *                                             - SIGKILL
 *       +--> unlock
 *       +--> sleep briefly unless current is MEMDIE
 */

/*****************************************************************************/
/* 26. FAST ASCII FLOW                                                        */
/*****************************************************************************/

/*
 *                allocation failure
 *                       |
 *                       v
 *                out_of_memory()
 *                       |
 *          +------------+-------------+
 *          |                          |
 *          v                          v
 *   notifier frees memory?         no memory freed
 *          |                          |
 *         yes                         v
 *          |                    constrained_alloc()
 *          v                          |
 *       return                +-------+--------+----------------+
 *                              |                |                |
 *                              v                v                v
 *                       MPOL_BIND fail     CPUSET fail     global OOM
 *                              |                |                |
 *                              v                v                v
 *                        kill current      kill current   select_bad_process
 *                                                                  |
 *                                                                  v
 *                                                          oom_kill_process
 *                                                                  |
 *                                                +-----------------+----------------+
 *                                                |                                  |
 *                                                v                                  v
 *                                         kill a child first                  kill selected task
 *                                                                                  |
 *                                                                                  v
 *                                                                     set TIF_MEMDIE + SIGKILL
 */

/*****************************************************************************/
/* 27. IMPORTANT REVIEW POINTERS                                              */
/*****************************************************************************/

/*
 * When you study this file, pay special attention to:
 *
 *   1. badness()
 *      -> understand every factor changing score
 *
 *   2. select_bad_process()
 *      -> understand why existing exiting/MEMDIE tasks suppress new kills
 *
 *   3. __oom_kill_task()
 *      -> TIF_MEMDIE is the key mechanism
 *
 *   4. constrained_alloc()
 *      -> explains why current is killed in policy/cpuset cases
 *
 *   5. out_of_memory()
 *      -> entire top-level orchestration is here
 */

/*****************************************************************************/
/* 28. SUBTLE POINTS / CAVEATS                                                */
/*****************************************************************************/

/*
 * A) total_vm vs real memory use
 *    ---------------------------
 *    badness() starts from mm->total_vm, which is virtual memory footprint,
 *    not a perfect “resident bytes to be freed” metric.
 *    But it is cheap and broadly useful as heuristic.
 *
 * B) child contribution
 *    ------------------
 *    This helps catch forking servers, but it is still a heuristic.
 *
 * C) cpuset overlap penalty
 *    ----------------------
 *    A task outside current cpuset may still matter, so score is reduced, not
 *    forced to zero.
 *
 * D) exiting task handling
 *    ---------------------
 *    The code prefers patience over immediate multiple kills.
 */

/*****************************************************************************/
/* 29. IF YOU NEED A 30-SECOND EXPLANATION                                    */
/*****************************************************************************/

/*
 * Linux 2.6.20 oom_kill.c is the emergency last-resort victim selection code.
 * It computes a badness score mostly from memory footprint, adjusted by child
 * memory, runtime, CPU time, privilege, niceness, cpuset overlap, and
 * oomkilladj. Then out_of_memory() either kills current for constrained OOMs
 * or scans the whole system for the highest-scoring victim. The chosen task is
 * marked TIF_MEMDIE so it can access reserves and die fast, then SIGKILLed.
 */

/*****************************************************************************/
/* 30. FINAL TAKEAWAY                                                         */
/*****************************************************************************/

/*
 * The most important conceptual sentence for this file is:
 *
 *   “The OOM killer is not just about killing a large process; it is about
 *    choosing a victim that is most likely to free useful memory with the
 *    least surprise, while ensuring that the chosen victim can actually exit
 *    by giving it reserve access through TIF_MEMDIE.”
 */

/*****************************************************************************/
/* END OF NOTES                                                               */
/*****************************************************************************/

