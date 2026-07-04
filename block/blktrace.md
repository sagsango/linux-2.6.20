/*
 * FILE: block/blktrace.c.notes
 *
 * SUBJECT:
 *     IDE-style walkthrough of Linux 2.6 blktrace implementation.
 *
 * SOURCE:
 *     The uploaded source is the Linux block trace implementation.
 *
 * PURPOSE:
 *     Explain what blktrace does, where it fits in the block layer,
 *     how tracing is set up, how events are emitted, how relay/debugfs
 *     are used, and how the trace lifecycle works.
 */

/*
 * ============================================================
 * 0. QUICK SUMMARY
 * ============================================================
 *
 * blktrace records block I/O events from inside the kernel.
 *
 * It captures events like:
 *
 *     - request queued
 *     - request merged
 *     - request issued to driver
 *     - request completed
 *     - read/write direction
 *     - sector number
 *     - byte count
 *     - process ID
 *     - CPU
 *     - timestamp
 *
 * User space tools can then read these events through debugfs/relayfs.
 *
 * Typical user-space flow:
 *
 *     blktrace -d /dev/sda
 *     blkparse ...
 *
 * Kernel flow:
 *
 *     block layer event
 *          |
 *          v
 *     __blk_add_trace()
 *          |
 *          v
 *     relay buffer
 *          |
 *          v
 *     debugfs file
 *          |
 *          v
 *     user-space blktrace/blkparse
 */

/*
 * ============================================================
 * 1. WHY BLKTRACE EXISTS
 * ============================================================
 *
 * The block layer is complicated:
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
 *     request queue
 *        |
 *        v
 *     I/O scheduler
 *        |
 *        v
 *     device driver
 *        |
 *        v
 *     disk
 *
 * Performance problems can occur at any step:
 *
 *     - too many small I/Os
 *     - bad merging
 *     - seek-heavy pattern
 *     - high latency
 *     - scheduler issue
 *     - driver/device delay
 *
 * blktrace provides a low-overhead way to see the real request stream.
 */

/*
 * ============================================================
 * 2. MAJOR COMPONENTS
 * ============================================================
 *
 * struct blk_trace:
 *
 *     Per-device tracing state.
 *
 *     Contains:
 *         relay channel
 *         debugfs directory
 *         action mask filter
 *         pid filter
 *         LBA range filter
 *         dropped event counter
 *         per-CPU sequence counters
 *         trace state
 *
 *
 * struct blk_io_trace:
 *
 *     One trace event record written into relay buffer.
 *
 *     Contains:
 *         magic/version
 *         sequence
 *         timestamp
 *         sector
 *         bytes
 *         action
 *         pid
 *         device
 *         cpu
 *         error
 *         pdu_len
 *
 *
 * relay channel:
 *
 *     Per-CPU high-speed trace buffers.
 *
 *
 * debugfs:
 *
 *     Exposes trace buffers to user space.
 */

/*
 * ============================================================
 * 3. TRACE LIFECYCLE
 * ============================================================
 *
 *     BLKTRACESETUP
 *          |
 *          v
 *     allocate blk_trace
 *     create debugfs directory
 *     create relay channel
 *     attach bt to q->blk_trace
 *
 *          |
 *          v
 *     BLKTRACESTART
 *          |
 *          v
 *     trace_state = Blktrace_running
 *     write timestamp note
 *
 *          |
 *          v
 *     block I/O events happen
 *          |
 *          v
 *     __blk_add_trace()
 *     writes records into relay buffer
 *
 *          |
 *          v
 *     BLKTRACESTOP
 *          |
 *          v
 *     trace_state = Blktrace_stopped
 *     relay_flush()
 *
 *          |
 *          v
 *     BLKTRACETEARDOWN
 *          |
 *          v
 *     remove trace from queue
 *     close relay channel
 *     remove debugfs files
 *     free memory
 */

/*
 * ============================================================
 * 4. GLOBAL VARIABLES
 * ============================================================
 *
 * blk_trace_cpu_offset:
 *
 *     Per-CPU time offset.
 *
 *     Used to normalize sched_clock() timestamps against wall time.
 *
 *
 * blktrace_seq:
 *
 *     Global trace generation counter.
 *
 *     Used so each task emits a process-name note once per trace session.
 *
 *
 * blk_tree_root:
 *
 *     debugfs root:
 *
 *         /sys/kernel/debug/block
 *
 *
 * blk_tree_mutex:
 *
 *     Protects debugfs tree creation/removal.
 *
 *
 * root_users:
 *
 *     Number of active block trace directories under debugfs root.
 */

/*
 * ============================================================
 * 5. trace_note()
 * ============================================================
 *
 * Purpose:
 *     Write a notification record into relay buffer.
 *
 * Used for:
 *
 *     - process name notes
 *     - timestamp notes
 *
 * Flow:
 *
 *     trace_note(bt, pid, action, data, len)
 *          |
 *          v
 *     reserve relay buffer space
 *          |
 *          v
 *     fill blk_io_trace header
 *          |
 *          v
 *     copy note payload after header
 *
 * Important fields:
 *
 *     t->magic
 *         identifies trace record format/version
 *
 *     t->time
 *         sched_clock() adjusted by CPU offset
 *
 *     t->device
 *         device number
 *
 *     t->action
 *         note type
 *
 *     t->pid
 *         process ID
 *
 *     t->cpu
 *         CPU that emitted event
 *
 *     t->pdu_len
 *         length of payload after trace header
 */

/*
 * ============================================================
 * 6. trace_note_tsk()
 * ============================================================
 *
 * Purpose:
 *     Emit process-name note for a task.
 *
 * Code idea:
 *
 *     task->btrace_seq = blktrace_seq;
 *     trace_note(..., BLK_TN_PROCESS, task->comm, sizeof(task->comm));
 *
 * Why?
 *
 *     Trace events store PID, but user space also wants process names.
 *
 * Instead of writing process name on every event, blktrace writes it once
 * per task per tracing generation.
 */

/*
 * ============================================================
 * 7. trace_note_time()
 * ============================================================
 *
 * Purpose:
 *     Emit a wall-clock timestamp note at trace start.
 *
 * It stores:
 *
 *     seconds
 *     nanoseconds
 *
 * This helps user space correlate sched_clock-based trace timestamps with
 * real time.
 */

/*
 * ============================================================
 * 8. act_log_check()
 * ============================================================
 *
 * Purpose:
 *     Apply trace filters.
 *
 * Filters:
 *
 *     action mask
 *     sector range
 *     pid
 *
 * Return:
 *
 *     0 = log event
 *     1 = skip event
 */

/*
 * ============================================================
 * 9. __blk_add_trace()
 * ============================================================
 *
 * Purpose:
 *     Main event emission function.
 *
 * This is the hot path.
 *
 * Called by block-layer tracepoints/macros when I/O events happen.
 *
 * Simplified flow:
 *
 *     __blk_add_trace(bt, sector, bytes, rw, what, error, pdu_len, pdu)
 *          |
 *          v
 *     if trace not running:
 *         return
 *
 *     add direction/action bits
 *
 *     pid = current->pid
 *
 *     if filters reject event:
 *         return
 *
 *     disable local IRQs
 *
 *     if current task has not emitted process note:
 *         trace_note_tsk()
 *
 *     reserve relay space
 *
 *     fill blk_io_trace:
 *         magic/version
 *         sequence
 *         time
 *         sector
 *         bytes
 *         action
 *         pid
 *         device
 *         cpu
 *         error
 *         pdu_len
 *
 *     copy optional payload
 *
 *     enable local IRQs
 */

/*
 * Why local_irq_save()?
 *
 * The relay buffer is per-CPU.
 *
 * If an interrupt happened while the current context was reserving/filling
 * a per-CPU relay slot, interrupt code on the same CPU could also trace
 * and corrupt ordering/reservation.
 */

/*
 * ============================================================
 * 10. DEBUGFS TREE MANAGEMENT
 * ============================================================
 *
 * blk_create_tree(name):
 *
 *     creates:
 *
 *         /sys/kernel/debug/block
 *         /sys/kernel/debug/block/<device>
 *
 *
 * blk_remove_tree(dir):
 *
 *     removes device directory.
 *
 *     If no users remain, removes root too.
 *
 *
 * root_users:
 *
 *     tracks how many devices are being traced.
 */

/*
 * ============================================================
 * 11. CLEANUP AND REMOVE
 * ============================================================
 *
 * blk_trace_cleanup():
 *
 *     Frees/removes:
 *
 *         relay channel
 *         dropped debugfs file
 *         device debugfs directory
 *         per-CPU sequence counters
 *         blk_trace object
 *
 *
 * blk_trace_remove():
 *
 *     Detaches tracing from request_queue.
 *
 *     Important:
 *
 *         bt = xchg(&q->blk_trace, NULL);
 *
 *     This atomically removes the trace pointer from the queue.
 */

/*
 * ============================================================
 * 12. dropped FILE
 * ============================================================
 *
 * debugfs file:
 *
 *     dropped
 *
 * Shows:
 *
 *     number of lost trace records
 *
 *
 * If relay subbuffer is full:
 *
 *     blk_subbuf_start_callback()
 *         |
 *         v
 *     atomic_inc(&bt->dropped)
 *
 * User space can read:
 *
 *     /sys/kernel/debug/block/<dev>/dropped
 */

/*
 * ============================================================
 * 13. RELAY CALLBACKS
 * ============================================================
 *
 * blk_subbuf_start_callback():
 *
 *     Called when relay moves to a new subbuffer.
 *
 *     If buffer is full:
 *
 *         increment dropped counter
 *         return 0
 *
 *
 * blk_create_buf_file_callback():
 *
 *     Creates debugfs file for relay buffer.
 *
 *
 * blk_remove_buf_file_callback():
 *
 *     Removes relay debugfs file.
 */

/*
 * ============================================================
 * 14. blk_trace_setup()
 * ============================================================
 *
 * Purpose:
 *     Handle BLKTRACESETUP ioctl.
 *
 * Main work:
 *
 *     copy setup structure from user
 *     validate buffer size/count
 *     normalize device name
 *     allocate struct blk_trace
 *     allocate per-CPU sequence counters
 *     create debugfs directory
 *     create dropped file
 *     open relay channel
 *     store filters
 *     attach trace to request_queue
 *
 * Flow:
 *
 *     user ioctl BLKTRACESETUP
 *          |
 *          v
 *     blk_trace_ioctl()
 *          |
 *          v
 *     blk_trace_setup()
 *          |
 *          v
 *     q->blk_trace = bt
 */

/*
 * ============================================================
 * 15. blk_trace_startstop()
 * ============================================================
 *
 * Purpose:
 *     Start or stop tracing.
 *
 * Start:
 *
 *     valid states:
 *
 *         Blktrace_setup
 *         Blktrace_stopped
 *
 *     actions:
 *
 *         blktrace_seq++
 *         memory barrier
 *         trace_state = Blktrace_running
 *         write timestamp note
 *
 *
 * Stop:
 *
 *     valid state:
 *
 *         Blktrace_running
 *
 *     actions:
 *
 *         trace_state = Blktrace_stopped
 *         relay_flush()
 */

/*
 * ============================================================
 * 16. blk_trace_ioctl()
 * ============================================================
 *
 * Purpose:
 *     Main user-space control interface.
 *
 * Supported ioctls:
 *
 *     BLKTRACESETUP
 *         allocate and configure trace
 *
 *     BLKTRACESTART
 *         start recording
 *
 *     BLKTRACESTOP
 *         stop recording
 *
 *     BLKTRACETEARDOWN
 *         remove and cleanup trace
 */

/*
 * ============================================================
 * 17. TIMESTAMP CALIBRATION
 * ============================================================
 *
 * Trace records use:
 *
 *     sched_clock()
 *
 * But user-visible time correlation needs wall-clock time.
 *
 * This file computes per-CPU offsets:
 *
 *     wall_time_ns - sched_clock_ns
 *
 * blk_check_time():
 *
 *     a = sched_clock()
 *     gettimeofday()
 *     b = sched_clock()
 *
 *     estimated sched time at gettimeofday:
 *
 *         (a + b) / 2
 *
 *     offset:
 *
 *         wall_time - estimated_sched_time
 *
 * blk_trace_check_cpu_time():
 *
 *     runs calibration on each CPU.
 *
 * blk_trace_set_ht_offsets():
 *
 *     makes SMT siblings share same offset.
 */

/*
 * ============================================================
 * 18. blk_trace_init()
 * ============================================================
 *
 * Module/init function.
 *
 * Does:
 *
 *     mutex_init(&blk_tree_mutex)
 *     on_each_cpu(blk_trace_check_cpu_time)
 *     blk_trace_set_ht_offsets()
 *
 * Registered by:
 *
 *     module_init(blk_trace_init)
 */

/*
 * ============================================================
 * 19. FULL EVENT FLOW
 * ============================================================
 *
 *     Process submits I/O
 *          |
 *          v
 *     block layer event occurs
 *          |
 *          v
 *     trace macro calls __blk_add_trace()
 *          |
 *          v
 *     action bits are built
 *          |
 *          v
 *     filters checked:
 *         action / sector / pid
 *          |
 *          v
 *     relay_reserve()
 *          |
 *          v
 *     blk_io_trace record filled
 *          |
 *          v
 *     user reads debugfs relay file
 *          |
 *          v
 *     blkparse decodes events
 */

/*
 * ============================================================
 * 20. FULL CONTROL FLOW
 * ============================================================
 *
 *     user: BLKTRACESETUP
 *          |
 *          v
 *     blk_trace_setup()
 *          |
 *          v
 *     q->blk_trace = bt
 *
 *     user: BLKTRACESTART
 *          |
 *          v
 *     bt->trace_state = running
 *
 *     block I/O happens
 *          |
 *          v
 *     __blk_add_trace()
 *          |
 *          v
 *     relay buffer gets events
 *
 *     user: BLKTRACESTOP
 *          |
 *          v
 *     bt->trace_state = stopped
 *     relay_flush()
 *
 *     user: BLKTRACETEARDOWN
 *          |
 *          v
 *     blk_trace_remove()
 *     blk_trace_cleanup()
 */

/*
 * ============================================================
 * 21. IMPORTANT DESIGN POINTS
 * ============================================================
 *
 * 1. Low overhead:
 *
 *     Uses per-CPU relay buffers.
 *
 *
 * 2. Safe from interrupt reentry:
 *
 *     Disables local IRQs while reserving/writing event record.
 *
 *
 * 3. Filtered:
 *
 *     Can filter by action, LBA range, and PID.
 *
 *
 * 4. Debugfs-based:
 *
 *     User space reads trace streams from debugfs.
 *
 *
 * 5. Per-CPU timing:
 *
 *     Calibrates CPU offsets for better timestamp correlation.
 *
 *
 * 6. Drop accounting:
 *
 *     Counts events lost due to full relay buffers.
 */

/*
 * ============================================================
 * 22. RELATION TO BLOCK LAYER
 * ============================================================
 *
 * blktrace does not schedule I/O.
 * blktrace does not merge I/O.
 * blktrace does not submit I/O.
 *
 * It only observes and records events.
 *
 * Relationship:
 *
 *     I/O scheduler:
 *         decides order
 *
 *     block driver:
 *         sends commands to hardware
 *
 *     blktrace:
 *         records what happens
 */

/*
 * ============================================================
 * 23. FINAL MENTAL MODEL
 * ============================================================
 *
 * blktrace is a per-block-device tracing system.
 *
 * User space sets it up through ioctls.
 * Kernel stores events in per-CPU relay buffers.
 * debugfs exposes those buffers.
 * blkparse decodes the binary records.
 *
 * One-line summary:
 *
 *     blktrace.c implements low-overhead block I/O event tracing by
 *     attaching a blk_trace object to a request_queue, writing
 *     blk_io_trace records into per-CPU relay buffers, exposing them
 *     through debugfs, and controlling the lifecycle via block-device
 *     ioctls.
 */

