/*
 * FILE: block/cfq-iosched.c.notes
 *
 * SUBJECT:
 *     Short IDE-style notes for Linux 2.6 CFQ I/O scheduler.
 *
 * SOURCE:
 *     Uploaded CFQ source: "CFQ, or complete fairness queueing, disk scheduler."
 *
 * PURPOSE:
 *     Give a compact but useful background, code walk, and flow explanation
 *     for studying CFQ without turning it into a book.
 */

/*
 * ============================================================
 * 0. QUICK SUMMARY
 * ============================================================
 *
 * CFQ = Completely Fair Queuing.
 *
 * It is a disk I/O scheduler.
 *
 * Main idea:
 *
 *     Instead of keeping one global request queue, CFQ creates separate
 *     queues per process / I/O context and gives each queue a fair time
 *     slice on the disk.
 *
 * It tries to balance:
 *
 *     - fairness between processes
 *     - low latency for synchronous reads
 *     - throughput for writes
 *     - disk seek reduction
 *     - I/O priority support
 */

/*
 * ============================================================
 * 1. WHERE CFQ FITS
 * ============================================================
 *
 * Block I/O path:
 *
 *     process
 *        |
 *        v
 *     filesystem
 *        |
 *        v
 *     bio
 *        |
 *        v
 *     request
 *        |
 *        v
 *     I/O scheduler  <---- CFQ lives here
 *        |
 *        v
 *     block driver
 *        |
 *        v
 *     disk
 *
 * CFQ decides:
 *
 *     which request should be sent to the driver next?
 */

/*
 * ============================================================
 * 2. WHY CFQ EXISTS
 * ============================================================
 *
 * Problem with one global elevator:
 *
 *     One process doing heavy I/O can dominate the disk.
 *
 * Example:
 *
 *     Process A: huge write stream
 *     Process B: interactive read
 *
 * Without fairness:
 *
 *     B may wait too long.
 *
 * CFQ solution:
 *
 *     Process A gets a queue.
 *     Process B gets a queue.
 *     Scheduler rotates between queues.
 *
 * So each process gets fair disk access.
 */

/*
 * ============================================================
 * 3. MAIN DATA STRUCTURES
 * ============================================================
 *
 * struct cfq_data
 *
 *     Per block device / request_queue scheduler state.
 *
 *     Important fields:
 *
 *         queue
 *             owning request_queue
 *
 *         rr_list[]
 *             round-robin lists by best-effort priority
 *
 *         busy_rr
 *             queues with dispatched requests still in driver
 *
 *         cur_rr
 *             current round-robin list being served
 *
 *         idle_rr
 *             idle-priority queues
 *
 *         active_queue
 *             currently served cfq_queue
 *
 *         cfq_hash
 *             lookup table for cfq_queue
 *
 *         rq_in_driver
 *             requests currently sent to driver
 *
 *         idle_slice_timer
 *             timer used when CFQ idles waiting for next sync request
 *
 *         unplug_work
 *             work item to restart queue
 *
 *         last_sector
 *             last dispatched sector
 *
 *         tunables
 *             quantum, slices, fifo expiry, back seek penalty, etc.
 *
 *
 * struct cfq_queue
 *
 *     Per process / per priority / per sync-async queue.
 *
 *     Important fields:
 *
 *         sort_list
 *             rb-tree of requests sorted by sector
 *
 *         fifo
 *             FIFO list for expiration/starvation prevention
 *
 *         next_rq
 *             cached best next request
 *
 *         queued[]
 *             queued sync/async count
 *
 *         on_dispatch[]
 *             requests currently dispatched to driver
 *
 *         slice_start / slice_end / slice_left
 *             time slice accounting
 *
 *         ioprio / ioprio_class
 *             I/O priority
 *
 *         flags
 *             state bits: on_rr, wait_request, idle_window, etc.
 *
 *
 * struct cfq_io_context
 *
 *     Per task/per device CFQ context.
 *
 *     Tracks:
 *
 *         associated cfq queues
 *         think time
 *         seek history
 *         priority changes
 */

/*
 * ============================================================
 * 4. QUEUE MODEL
 * ============================================================
 *
 * CFQ does not mainly schedule individual requests globally.
 * It schedules queues.
 *
 *     task/process
 *         |
 *         v
 *     cfq_io_context
 *         |
 *         v
 *     cfq_queue
 *         |
 *         v
 *     requests in rb-tree + fifo
 *
 *
 * Diagram:
 *
 *     cfq_data
 *       |
 *       +-- cfq_queue for process A
 *       |       |
 *       |       +-- rq sector 100
 *       |       +-- rq sector 120
 *       |
 *       +-- cfq_queue for process B
 *       |       |
 *       |       +-- rq sector 9000
 *       |
 *       +-- cfq_queue for async writes
 */

/*
 * ============================================================
 * 5. SYNC VS ASYNC
 * ============================================================
 *
 * CFQ treats sync and async differently.
 *
 * Sync I/O:
 *
 *     Usually reads or synchronous writes.
 *     Often process is waiting.
 *     Latency sensitive.
 *     Gets per-process queues.
 *
 * Async I/O:
 *
 *     Usually buffered writes.
 *     Less latency sensitive.
 *     Often grouped into async queue.
 *
 *
 * cfq_queue_pid():
 *
 *     if read or sync write:
 *         key = task->pid
 *
 *     else:
 *         key = CFQ_KEY_ASYNC
 */

/*
 * ============================================================
 * 6. I/O PRIORITY
 * ============================================================
 *
 * CFQ supports Linux I/O priorities:
 *
 *     IOPRIO_CLASS_RT
 *         real-time I/O
 *
 *     IOPRIO_CLASS_BE
 *         best-effort I/O
 *
 *     IOPRIO_CLASS_IDLE
 *         only run when disk is otherwise idle
 *
 *
 * Best-effort priority levels:
 *
 *     0 highest
 *     7 lowest
 *
 * CFQ maps priority to:
 *
 *     - which round-robin list queue enters
 *     - how long its slice is
 *     - how many async requests it may dispatch
 */

/*
 * ============================================================
 * 7. REQUEST STORAGE INSIDE A CFQ QUEUE
 * ============================================================
 *
 * Each cfq_queue keeps requests in two structures:
 *
 * 1. rb-tree:
 *
 *     sorted by sector
 *     used for seek-efficient dispatch
 *
 * 2. fifo list:
 *
 *     sorted by insertion time
 *     used for request expiration
 *
 *
 * Diagram:
 *
 *     cfq_queue
 *
 *       sort_list rb-tree:
 *
 *             sector 100
 *             sector 120
 *             sector 7000
 *
 *       fifo:
 *
 *             oldest rq -> newer rq -> newest rq
 */

/*
 * ============================================================
 * 8. cfq_choose_req()
 * ============================================================
 *
 * Purpose:
 *     Choose the better of two requests.
 *
 * Policy:
 *
 *     1. Prefer sync over async.
 *     2. Prefer metadata over normal data.
 *     3. Prefer request closest to current disk head.
 *     4. Allow small backward seeks, but penalize them.
 *
 *
 * Important tunables:
 *
 *     cfq_back_max
 *         maximum allowed backward seek
 *
 *     cfq_back_penalty
 *         backward seek cost multiplier
 *
 *
 * Mental model:
 *
 *     CFQ still behaves like an elevator inside each cfq_queue.
 */

/*
 * ============================================================
 * 9. ROUND-ROBIN QUEUE LISTS
 * ============================================================
 *
 * CFQ schedules queues using round-robin lists.
 *
 * Important lists:
 *
 *     rr_list[prio]
 *         best-effort queues grouped by priority
 *
 *     cur_rr
 *         currently selected priority round
 *
 *     busy_rr
 *         queues with requests already in driver
 *
 *     idle_rr
 *         idle-class queues
 *
 *
 * Flow:
 *
 *     cfq_queue gets request
 *          |
 *          v
 *     cfq_add_cfqq_rr()
 *          |
 *          v
 *     queue enters proper RR list
 *          |
 *          v
 *     cfq_select_queue()
 *          |
 *          v
 *     queue gets active time slice
 */

/*
 * ============================================================
 * 10. TIME SLICES
 * ============================================================
 *
 * CFQ gives each active queue a time slice.
 *
 * Fields:
 *
 *     slice_start
 *     slice_end
 *     slice_left
 *
 *
 * Sync queues usually get longer slice:
 *
 *     cfq_slice_sync
 *
 * Async queues get shorter slice:
 *
 *     cfq_slice_async
 *
 *
 * If a queue uses its slice:
 *
 *     cfq_slice_expired()
 *
 * Then queue goes back to RR list or disappears if empty.
 */

/*
 * ============================================================
 * 11. IDLE WINDOW
 * ============================================================
 *
 * CFQ may intentionally idle after a sync request.
 *
 * Why?
 *
 *     Same process may submit next nearby read soon.
 *
 * This is similar in spirit to anticipatory scheduling.
 *
 *
 * Flow:
 *
 *     sync queue becomes empty
 *          |
 *          v
 *     queue still has slice left
 *          |
 *          v
 *     CFQ arms idle_slice_timer
 *          |
 *          v
 *     if same queue submits more I/O:
 *         dispatch it
 *
 *     if timer expires:
 *         expire queue and choose another
 *
 *
 * CFQ disables idling if:
 *
 *     - hardware supports deep queueing
 *     - process is seeky
 *     - process think time is too long
 *     - idle tunable is disabled
 */

/*
 * ============================================================
 * 12. REQUEST INSERT PATH
 * ============================================================
 *
 * Main functions:
 *
 *     cfq_set_request()
 *     cfq_insert_request()
 *     cfq_rq_enqueued()
 *
 *
 * Flow:
 *
 *     new request allocated
 *          |
 *          v
 *     cfq_set_request()
 *          |
 *          +--> get task io_context
 *          +--> get/create cfq_io_context
 *          +--> get/create cfq_queue
 *          +--> attach cfq_queue to request
 *
 *     request inserted
 *          |
 *          v
 *     cfq_insert_request()
 *          |
 *          +--> insert into rb-tree
 *          +--> insert into fifo list
 *          +--> update next_rq
 *          +--> update think/seek history
 *          +--> maybe preempt active queue
 */

/*
 * ============================================================
 * 13. DISPATCH PATH
 * ============================================================
 *
 * Main functions:
 *
 *     cfq_dispatch_requests()
 *     cfq_select_queue()
 *     __cfq_dispatch_requests()
 *     cfq_dispatch_insert()
 *
 *
 * Flow:
 *
 *     block layer asks scheduler for request
 *          |
 *          v
 *     cfq_dispatch_requests()
 *          |
 *          v
 *     cfq_select_queue()
 *          |
 *          +--> keep active queue if slice valid
 *          +--> otherwise choose next queue from RR lists
 *
 *          |
 *          v
 *     __cfq_dispatch_requests()
 *          |
 *          +--> choose expired FIFO request if needed
 *          +--> otherwise choose next_rq
 *          +--> dispatch up to quantum requests
 *
 *          |
 *          v
 *     cfq_dispatch_insert()
 *          |
 *          +--> remove request from cfq_queue
 *          +--> put on driver dispatch list
 *          +--> update last_sector
 */

/*
 * ============================================================
 * 14. COMPLETION PATH
 * ============================================================
 *
 * Main function:
 *
 *     cfq_completed_request()
 *
 *
 * When driver completes request:
 *
 *     decrement rq_in_driver
 *     decrement cfqq->on_dispatch[]
 *     update last_end_request
 *     update sync task's last_end_request
 *     maybe expire active queue
 *     maybe arm idle timer if queue is empty
 *
 *
 * Important:
 *
 *     completion influences whether CFQ waits for more I/O from same queue.
 */

/*
 * ============================================================
 * 15. MERGE PATH
 * ============================================================
 *
 * Main functions:
 *
 *     cfq_merge()
 *     cfq_merged_request()
 *     cfq_merged_requests()
 *     cfq_allow_merge()
 *
 *
 * Purpose:
 *
 *     Combine adjacent requests to reduce overhead and improve sequential I/O.
 *
 *
 * CFQ only allows merge when:
 *
 *     - merge is valid at elevator level
 *     - request belongs to same cfq_queue
 *     - sync bio is not merged into async request
 */

/*
 * ============================================================
 * 16. PREEMPTION
 * ============================================================
 *
 * Main functions:
 *
 *     cfq_should_preempt()
 *     cfq_preempt_queue()
 *
 *
 * A new queue may preempt active queue if:
 *
 *     - active queue is idle class
 *     - new request is sync and active queue is async
 *     - new request is metadata and current queue has no metadata pending
 *     - new queue has slice left
 *
 *
 * Goal:
 *
 *     improve latency for important sync/metadata I/O.
 */

/*
 * ============================================================
 * 17. THINKTIME AND SEEK HISTORY
 * ============================================================
 *
 * Main functions:
 *
 *     cfq_update_io_thinktime()
 *     cfq_update_io_seektime()
 *     cfq_update_idle_window()
 *
 *
 * CFQ learns process behavior:
 *
 *     thinktime:
 *         delay between I/O completions and next I/O
 *
 *     seek distance:
 *         how far apart requests are
 *
 *
 * If process is sequential and quick:
 *
 *     idling may help
 *
 * If process is seeky or slow:
 *
 *     idling is disabled
 */

/*
 * ============================================================
 * 18. TIMERS AND WORKQUEUE
 * ============================================================
 *
 * idle_slice_timer:
 *
 *     used when active sync queue is empty but CFQ waits briefly for
 *     more requests from same queue.
 *
 *
 * idle_class_timer:
 *
 *     delays idle-class queues until non-idle queues had grace period.
 *
 *
 * unplug_work:
 *
 *     kblockd work item that restarts queue dispatch.
 */

/*
 * ============================================================
 * 19. INITIALIZATION
 * ============================================================
 *
 * Main function:
 *
 *     cfq_init_queue()
 *
 *
 * It allocates and initializes:
 *
 *     cfq_data
 *     rr lists
 *     busy/current/idle lists
 *     cfq hash table
 *     timers
 *     work item
 *     tunables
 *
 *
 * Slab caches:
 *
 *     cfq_pool
 *         cache for cfq_queue
 *
 *     cfq_ioc_pool
 *         cache for cfq_io_context
 */

/*
 * ============================================================
 * 20. CLEANUP
 * ============================================================
 *
 * Main functions:
 *
 *     cfq_exit_queue()
 *     cfq_put_queue()
 *     cfq_exit_io_context()
 *     cfq_free_io_context()
 *
 *
 * Cleanup does:
 *
 *     stop timers
 *     flush queue work
 *     expire active queue
 *     detach cfq_io_context objects
 *     free cfq queues
 *     free hash table
 *     free cfq_data
 */

/*
 * ============================================================
 * 21. SYSFS TUNABLES
 * ============================================================
 *
 * CFQ exposes tunables through elevator sysfs.
 *
 * Examples:
 *
 *     quantum
 *     fifo_expire_sync
 *     fifo_expire_async
 *     back_seek_max
 *     back_seek_penalty
 *     slice_idle
 *     slice_sync
 *     slice_async
 *     slice_async_rq
 *
 *
 * These tune fairness, latency, batching, and seek behavior.
 */

/*
 * ============================================================
 * 22. COMPLETE REQUEST FLOW
 * ============================================================
 *
 *     process submits I/O
 *          |
 *          v
 *     request allocated
 *          |
 *          v
 *     cfq_set_request()
 *          |
 *          +--> attach cfq_io_context
 *          +--> attach cfq_queue
 *
 *          |
 *          v
 *     cfq_insert_request()
 *          |
 *          +--> rb-tree
 *          +--> fifo list
 *          +--> RR queue list
 *
 *          |
 *          v
 *     cfq_dispatch_requests()
 *          |
 *          +--> select active queue
 *          +--> select request from queue
 *
 *          |
 *          v
 *     cfq_dispatch_insert()
 *          |
 *          v
 *     driver handles request
 *          |
 *          v
 *     cfq_completed_request()
 *          |
 *          +--> update accounting
 *          +--> maybe idle
 *          +--> maybe expire slice
 */

/*
 * ============================================================
 * 23. SIMPLE MENTAL MODEL
 * ============================================================
 *
 * Deadline scheduler:
 *
 *     fairness mostly between requests.
 *
 * Anticipatory scheduler:
 *
 *     waits for same process to issue nearby reads.
 *
 * CFQ:
 *
 *     fairness between process queues.
 *
 *
 * CFQ is like:
 *
 *     "Each process gets its own disk queue and a time slice."
 *
 *
 * Inside each queue:
 *
 *     requests are ordered like an elevator.
 *
 *
 * Across queues:
 *
 *     queues are served fairly using round-robin and priorities.
 */

/*
 * ============================================================
 * 24. ONE-LINE SUMMARY
 * ============================================================
 *
 * CFQ is a process-aware Linux block I/O scheduler that groups requests
 * into per-process/per-priority queues, serves those queues with time-sliced
 * fairness, orders requests inside each queue by sector, and uses idling,
 * preemption, and I/O priorities to improve interactive latency while still
 * maintaining disk throughput.
 */

