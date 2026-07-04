```c
/*
 * FILE: block/as-iosched.c
 * TOPIC: Anticipatory I/O scheduler
 *
 * Source: uploaded Anticipatory & deadline I/O scheduler code. :contentReference[oaicite:0]{index=0}
 *
 * ============================================================
 * 1. BIG IDEA
 * ============================================================
 *
 * This scheduler tries to reduce disk seeks.
 *
 * Old mechanical disks are slow when the head jumps around.
 *
 * Normal scheduler problem:
 *
 *      Process A reads block 100
 *      Disk completes it
 *      Scheduler immediately serves Process B block 900000
 *      Process A soon asks for block 101
 *
 * Bad:
 *
 *      disk head moved far away
 *
 * Anticipatory scheduler idea:
 *
 *      After a synchronous read completes, wait a tiny time.
 *
 * Why?
 *
 *      The same process may issue the next nearby read soon.
 *
 * So it “anticipates” the next request.
 *
 * ============================================================
 * 2. MAIN DATA STRUCTURE
 * ============================================================
 *
 * struct as_data
 *
 * One per request_queue.
 *
 * Contains:
 *
 *      sort_list[2]
 *          red-black trees sorted by sector
 *
 *      fifo_list[2]
 *          expiration-order lists
 *
 *      next_rq[2]
 *          cached next best request
 *
 *      last_sector[2]
 *          last dispatched sector for read/write
 *
 *      antic_status
 *          anticipation state machine
 *
 *      antic_timer
 *          timer for waiting
 *
 *      antic_work
 *          deferred queue restart work
 *
 *      io_context
 *          process we are anticipating for
 *
 *      fifo_expire[]
 *          deadline-like expiration
 *
 *      batch_expire[]
 *          read/write batch duration
 *
 *
 * Index meaning:
 *
 *      REQ_SYNC  = reads / synchronous requests
 *      REQ_ASYNC = writes / asynchronous requests
 */

/*
 * ============================================================
 * 3. REQUEST LIFETIME
 * ============================================================
 *
 *      bio arrives
 *          |
 *          v
 *      block layer creates/merges request
 *          |
 *          v
 *      as_add_request()
 *          |
 *          +--> attach io_context
 *          +--> update thinktime/seek history
 *          +--> insert into rb tree
 *          +--> insert into fifo list
 *          +--> maybe stop anticipation
 *
 *      later:
 *
 *      as_dispatch_request()
 *          |
 *          v
 *      choose request
 *          |
 *          v
 *      as_move_to_dispatch()
 *          |
 *          v
 *      driver request_fn
 *          |
 *          v
 *      disk completes request
 *          |
 *          v
 *      as_completed_request()
 *          |
 *          +--> update dispatched count
 *          +--> maybe start anticipation
 *          +--> release io_context
 */

/*
 * ============================================================
 * 4. TWO LISTS PER REQUEST
 * ============================================================
 *
 * Each queued request lives in:
 *
 *      1. rb tree
 *      2. fifo list
 *
 *
 * rb tree:
 *
 *      sorted by sector
 *      used for elevator seek optimization
 *
 *
 * fifo list:
 *
 *      sorted by insertion/expiry time
 *      used to prevent starvation
 *
 *
 * Diagram:
 *
 *      sort_list[READ]                    fifo_list[READ]
 *
 *          sector 100                         oldest
 *              |                                |
 *          sector 120                          v
 *              |                            rq A expires first
 *          sector 9000                     rq B
 *                                           rq C
 */

/*
 * ============================================================
 * 5. IO CONTEXT
 * ============================================================
 *
 * Anticipation is process-aware.
 *
 * The scheduler tracks:
 *
 *      which process submitted I/O
 *      how long it waits between reads
 *      how far its next read usually seeks
 *
 *
 * struct as_io_context stores:
 *
 *      ttime_mean
 *          average think time
 *
 *      seek_mean
 *          average seek distance
 *
 *      nr_queued
 *          requests queued by this task
 *
 *      nr_dispatched
 *          requests dispatched for this task
 *
 *      state bits:
 *
 *          AS_TASK_RUNNING
 *          AS_TASK_IOSTARTED
 *          AS_TASK_IORUNNING
 */

/*
 * ============================================================
 * 6. ANTICIPATION STATE MACHINE
 * ============================================================
 *
 * enum anticipation_status:
 *
 *      ANTIC_OFF
 *          normal operation
 *
 *      ANTIC_WAIT_REQ
 *          waiting for last dispatched read to complete
 *
 *      ANTIC_WAIT_NEXT
 *          last read completed; waiting for next nearby read
 *
 *      ANTIC_FINISHED
 *          stop waiting and dispatch something
 *
 *
 * Flow:
 *
 *      dispatch sync read from process P
 *          |
 *          v
 *      remember P in ad->io_context
 *          |
 *          v
 *      request completes
 *          |
 *          v
 *      start anticipation timer
 *          |
 *          v
 *      does P submit another close read soon?
 *
 *          yes:
 *              stop anticipation
 *              dispatch it
 *
 *          no:
 *              timeout
 *              dispatch something else
 */

/*
 * ============================================================
 * 7. WHY ANTICIPATE?
 * ============================================================
 *
 * Without anticipation:
 *
 *      read sector 100
 *      switch to write sector 900000
 *      read sector 101 arrives
 *      seek back
 *
 *
 * With anticipation:
 *
 *      read sector 100
 *      wait ~6ms
 *      read sector 101 arrives
 *      dispatch sector 101
 *
 *
 * Good for:
 *
 *      sequential readers
 *      applications doing blocking reads
 *      mechanical disks
 *
 * Bad for:
 *
 *      SSDs
 *      high queue-depth devices
 *      workloads where waiting wastes throughput
 */

/*
 * ============================================================
 * 8. as_choose_req()
 * ============================================================
 *
 * Purpose:
 *      Pick better of two requests in same direction.
 *
 * It compares sectors relative to:
 *
 *      ad->last_sector[data_dir]
 *
 *
 * Elevator behavior:
 *
 *      Prefer forward movement.
 *
 * But allow small backward seeks:
 *
 *      MAXBACK = 1MB
 *      BACK_PENALTY = 2
 *
 *
 * Meaning:
 *
 *      small backward seek is allowed,
 *      but counted as twice as expensive.
 *
 *
 * Example:
 *
 *      disk head at sector 1000
 *
 *      rq1 = sector 1100
 *      rq2 = sector 950
 *
 *      rq2 is behind by 50 sectors.
 *      penalty = 50 * 2 = 100
 *
 *      rq1 is ahead by 100.
 *
 *      tie-ish; scheduler may choose based on logic.
 */

/*
 * ============================================================
 * 9. as_find_next_rq()
 * ============================================================
 *
 * Purpose:
 *      Given last request, find next elevator candidate.
 *
 * It checks:
 *
 *      rb_next()
 *      rb_prev()
 *
 * Then calls:
 *
 *      as_choose_req()
 *
 *
 * This supports one-way elevator ordering while still allowing limited
 * backward seeks.
 */

/*
 * ============================================================
 * 10. ANTICIPATION FUNCTIONS
 * ============================================================
 *
 * as_antic_waitreq()
 *
 *      Start anticipation.
 *
 *      If last request is still running:
 *
 *          ANTIC_WAIT_REQ
 *
 *      If already completed:
 *
 *          ANTIC_WAIT_NEXT
 *
 *
 * as_antic_waitnext()
 *
 *      Arm timer:
 *
 *          antic_start + antic_expire
 *
 *      State:
 *
 *          ANTIC_WAIT_NEXT
 *
 *
 * as_antic_stop()
 *
 *      Stop waiting.
 *
 *      Delete timer if needed.
 *      Set:
 *
 *          ANTIC_FINISHED
 *
 *      Schedule kblockd work to restart queue.
 *
 *
 * as_antic_timeout()
 *
 *      Timer expired.
 *
 *      Stop anticipating.
 *      Update probability stats.
 *      Schedule queue restart.
 */

/*
 * ============================================================
 * 11. THINKTIME / SEEK HISTORY
 * ============================================================
 *
 * as_update_iohist()
 *
 * Called when new request is queued.
 *
 * Tracks:
 *
 *      thinktime:
 *          time between previous read completion and next read
 *
 *      seek distance:
 *          distance between previous request and this request
 *
 *
 * Used to decide:
 *
 *      Is this process likely to issue another request soon?
 *
 *
 * If process usually waits too long:
 *
 *      do not anticipate.
 *
 * If process usually submits nearby reads quickly:
 *
 *      anticipation is useful.
 */

/*
 * ============================================================
 * 12. as_can_break_anticipation()
 * ============================================================
 *
 * Purpose:
 *      Decide whether a newly arrived request is good enough to stop waiting.
 *
 * Break anticipation if:
 *
 *      request is from same process
 *
 *      anticipated process has more queued/dispatched I/O
 *
 *      request is close to expected disk position
 *
 *      anticipated process exited
 *
 *      thinktime is too large
 *
 *
 * This avoids waiting forever or waiting for a process that is unlikely
 * to submit useful I/O.
 */

/*
 * ============================================================
 * 13. as_can_anticipate()
 * ============================================================
 *
 * Purpose:
 *      Decide whether to dispatch now or keep waiting.
 *
 * Returns 1:
 *
 *      keep anticipating
 *
 * Returns 0:
 *
 *      dispatch request now
 *
 *
 * Do not anticipate if:
 *
 *      no io_context
 *      anticipation just finished
 *      current request is good enough
 */

/*
 * ============================================================
 * 14. as_dispatch_request()
 * ============================================================
 *
 * This is the scheduler’s main decision function.
 *
 * It decides what request should go to the driver.
 *
 *
 * Inputs:
 *
 *      q
 *          request queue
 *
 *      force
 *          force dispatch everything
 *
 *
 * Main logic:
 *
 *      1. If force:
 *             dispatch all reads and writes.
 *
 *      2. If anticipating:
 *             return 0.
 *
 *      3. Continue current read/write batch if valid.
 *
 *      4. If read batch and anticipation says wait:
 *             wait.
 *
 *      5. If FIFO expired:
 *             dispatch oldest expired request.
 *
 *      6. If batch expired:
 *             switch read/write direction.
 *
 *      7. Move selected request to dispatch.
 *
 *
 * High-level diagram:
 *
 *      as_dispatch_request()
 *          |
 *          v
 *      requests available?
 *          |
 *          +-- no -> return 0
 *          |
 *          v
 *      currently anticipating?
 *          |
 *          +-- yes -> return 0
 *          |
 *          v
 *      current batch still valid?
 *          |
 *          +-- yes:
 *          |       choose next_rq
 *          |       maybe anticipate if read
 *          |
 *          v
 *      batch expired?
 *          |
 *          +-- switch read/write
 *          |
 *          v
 *      fifo expired?
 *          |
 *          +-- choose oldest request
 *          |
 *          v
 *      as_move_to_dispatch()
 */

/*
 * ============================================================
 * 15. READ/WRITE BATCHING
 * ============================================================
 *
 * Reads and writes are separated.
 *
 * Reads are latency-sensitive.
 * Writes are throughput-friendly.
 *
 *
 * read batch:
 *
 *      serve reads for some time
 *      may anticipate next read
 *
 *
 * write batch:
 *
 *      serve multiple writes
 *      avoid starving writes forever
 *
 *
 * Fields:
 *
 *      batch_data_dir
 *          current direction
 *
 *      current_batch_expires
 *          when batch should end
 *
 *      write_batch_count
 *          target write batch size
 *
 *      current_write_count
 *          remaining writes this batch
 */

/*
 * ============================================================
 * 16. as_move_to_dispatch()
 * ============================================================
 *
 * Purpose:
 *      Move one selected request from scheduler queues to dispatch queue.
 *
 * Steps:
 *
 *      stop anticipation
 *      update last_sector
 *      remember io_context for sync read
 *      update next_rq cache
 *      remove from rb tree and fifo list
 *      put on dispatch queue
 *      mark AS_RQ_DISPATCHED
 *      increment dispatched counters
 */

/*
 * ============================================================
 * 17. ADD / REMOVE REQUEST
 * ============================================================
 *
 * as_add_request()
 *
 *      new request enters scheduler
 *
 *      steps:
 *
 *          set state NEW
 *          attach io_context
 *          update process I/O history
 *          insert into rb tree
 *          set FIFO expiry
 *          insert into fifo list
 *          update next_rq and anticipation state
 *          set state QUEUED
 *
 *
 * as_remove_queued_request()
 *
 *      remove from scheduler-owned queues
 *
 *      steps:
 *
 *          decrement queued count
 *          update next_rq if needed
 *          remove from fifo
 *          remove from rb tree
 */

/*
 * ============================================================
 * 18. MERGE LOGIC
 * ============================================================
 *
 * as_merge()
 *
 *      tries front merge.
 *
 * Example:
 *
 *      existing request:
 *
 *          sector 100 - 199
 *
 *      new bio:
 *
 *          sector 90 - 99
 *
 *      front merge creates:
 *
 *          sector 90 - 199
 *
 *
 * as_merged_request()
 *
 *      if front merge changed request sector,
 *      remove and reinsert in rb tree.
 *
 *
 * as_merged_requests()
 *
 *      two requests merged.
 *
 *      keeps earliest FIFO expiry.
 *      swaps io_context when needed.
 *      removes merged-away request.
 */

/*
 * ============================================================
 * 19. COMPLETION PATH
 * ============================================================
 *
 * as_completed_request()
 *
 * Called when driver completes a request.
 *
 * It:
 *
 *      verifies request state
 *      updates batch state
 *      decrements nr_dispatched
 *      starts anticipation after sync read completion
 *      updates io_context
 *      marks request postscheduled
 *
 *
 * Important anticipation point:
 *
 *      if completed request belongs to ad->io_context:
 *
 *          ad->antic_start = jiffies
 *          ad->ioc_finished = 1
 *
 *          if waiting for request completion:
 *              switch to waiting for next request
 */

/*
 * ============================================================
 * 20. kblockd WORK
 * ============================================================
 *
 * as_work_handler()
 *
 * Runs later in process context.
 *
 * It calls:
 *
 *      blk_start_queueing(q)
 *
 *
 * Why deferred?
 *
 *      Anticipation timer/work may need to restart queue safely.
 *      It can re-enter elevator code.
 */

/*
 * ============================================================
 * 21. as_may_queue()
 * ============================================================
 *
 * Purpose:
 *      Tell block layer if a task may/must queue.
 *
 * During anticipation:
 *
 *      if current task is the one we are waiting for:
 *
 *          ELV_MQUEUE_MUST
 *
 * Meaning:
 *
 *      allow that process to queue request immediately,
 *      because scheduler is waiting for it.
 */

/*
 * ============================================================
 * 22. INITIALIZATION
 * ============================================================
 *
 * as_init_queue()
 *
 * Allocates and initializes struct as_data.
 *
 * Sets defaults:
 *
 *      read_expire         = HZ / 8
 *      write_expire        = HZ / 4
 *      read_batch_expire   = HZ / 2
 *      write_batch_expire  = HZ / 8
 *      antic_expire        = HZ / 150
 *
 *
 * Initializes:
 *
 *      timer
 *      work item
 *      FIFO lists
 *      rb trees
 *      write batch count
 */

/*
 * ============================================================
 * 23. SYSFS TUNABLES
 * ============================================================
 *
 * Exposed scheduler attributes:
 *
 *      read_expire
 *      write_expire
 *      antic_expire
 *      read_batch_expire
 *      write_batch_expire
 *      est_time
 *
 *
 * These allow runtime tuning of:
 *
 *      request expiration
 *      anticipation wait
 *      read/write batch duration
 */

/*
 * ============================================================
 * 24. ELEVATOR REGISTRATION
 * ============================================================
 *
 * struct elevator_type iosched_as
 *
 * Registers callbacks:
 *
 *      elevator_merge_fn
 *      elevator_dispatch_fn
 *      elevator_add_req_fn
 *      elevator_completed_req_fn
 *      elevator_may_queue_fn
 *      elevator_init_fn
 *      elevator_exit_fn
 *
 *
 * Name:
 *
 *      "anticipatory"
 *
 *
 * module_init(as_init)
 *
 *      elv_register(&iosched_as)
 *
 *
 * module_exit(as_exit)
 *
 *      elv_unregister(&iosched_as)
 */

/*
 * ============================================================
 * 25. COMPLETE REQUEST FLOW
 * ============================================================
 *
 *      process issues read()
 *          |
 *          v
 *      bio submitted
 *          |
 *          v
 *      request created
 *          |
 *          v
 *      as_add_request()
 *          |
 *          +--> rb tree by sector
 *          +--> fifo list by expiry
 *          +--> io_context history
 *
 *      block layer asks for dispatch
 *          |
 *          v
 *      as_dispatch_request()
 *          |
 *          +--> choose read/write batch
 *          +--> choose elevator request
 *          +--> maybe anticipate
 *          +--> maybe dispatch
 *
 *      request dispatched
 *          |
 *          v
 *      device driver
 *          |
 *          v
 *      disk completes request
 *          |
 *          v
 *      as_completed_request()
 *          |
 *          +--> update process history
 *          +--> maybe start anticipation
 *
 *      if anticipated request arrives:
 *          |
 *          v
 *      as_antic_stop()
 *          |
 *          v
 *      dispatch it
 */

/*
 * ============================================================
 * 26. FINAL MENTAL MODEL
 * ============================================================
 *
 * This scheduler combines three ideas:
 *
 *      1. Elevator ordering
 *
 *          choose nearby sectors to reduce seek.
 *
 *      2. Deadlines/FIFO expiry
 *
 *          avoid starvation.
 *
 *      3. Anticipation
 *
 *          after a sync read, wait briefly for the same process to issue
 *          another nearby read.
 *
 *
 * One-line summary:
 *
 *      The anticipatory I/O scheduler is a process-aware disk scheduler
 *      that uses rb-tree elevator ordering, FIFO deadlines, read/write
 *      batching, and short idle anticipation after reads to reduce seek
 *      overhead on mechanical disks.
 */
```

