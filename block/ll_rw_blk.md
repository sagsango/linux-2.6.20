/*

FILE: ll_rw_blk.c (Linux 2.6) IDE STUDY NOTES (Short ~2 Pages)
============================================================

1. BACKGROUND

ll_rw_blk.c is the heart of the Linux 2.6 block layer.

Historically Linux evolved as:

    Buffer Cache
        |
        v
    BIO Layer
        |
        v
    Request Layer (ll_rw_blk.c)
        |
        v
    I/O Scheduler
        |
        v
    Block Driver
        |
        v
      Hardware

Filesystems do not talk directly to disk drivers. Instead they submit
BIOs which eventually arrive here.

Main responsibilities:

-   Allocate request queues
-   Convert BIOs into requests
-   Merge requests
-   Apply hardware limits
-   Interact with the elevator
-   Dispatch requests to drivers
-   Complete requests

Think of this file as the “traffic controller” of the block layer.

============================================================ 2.
IMPORTANT OBJECTS ————————————————————

BIO Describes one filesystem I/O.

REQUEST Hardware I/O. May contain multiple BIOs.

REQUEST_QUEUE One queue per block device.

ELEVATOR Scheduling policy (CFQ, Deadline, NOOP…).

============================================================ 3. COMPLETE
I/O FLOW ————————————————————

Application | read()/write() | Filesystem | submit_bio() |
generic_make_request() | ============== ll_rw_blk.c ================= |
BIO merge | Allocate Request | Request merge | Queue request | Elevator
(CFQ/Deadline) | request_fn() | Driver | DMA | Disk | Interrupt |
Request completion | Wake waiting process

============================================================ 4. REQUEST
QUEUE LIFECYCLE ————————————————————

Driver loads | blk_alloc_queue() | blk_init_queue() | Initialize limits
Initialize locks Initialize timers Initialize elevator | Queue ready

During runtime

BIO arrives | Merge? | Allocate request | Insert into scheduler |
Dispatch | Completion

Driver unload

blk_cleanup_queue()

============================================================ 5.
IMPORTANT SUBSYSTEMS ————————————————————

Queue Limits

Drivers advertise hardware capabilities such as maximum sectors, DMA
alignment, segment limits and logical sector size.

BIO/Request Merging

Adjacent BIOs or requests are merged to reduce seeks and increase
throughput.

Plug / Unplug

Requests are temporarily held so additional BIOs can arrive and be
merged before dispatch.

Tagged Queueing

Multiple outstanding requests are identified by hardware tags so
completions may occur out of order.

Scatter-Gather

Builds DMA scatterlists from BIO pages so hardware can transfer data
without copying.

============================================================ 6.
INTERACTION WITH THE ELEVATOR ————————————————————

ll_rw_blk.c does NOT decide scheduling policy.

Instead it calls the selected elevator.

             Request
                 |
                 v
         Elevator Framework
                 |
     +-----------+-----------+
     |           |           |
    CFQ      Deadline      NOOP

The elevator chooses which request to dispatch next.

============================================================ 7. COMMON
IMPORTANT FUNCTIONS ————————————————————

blk_alloc_queue() Allocate queue object.

blk_init_queue() Initialize queue.

generic_make_request() Entry point for BIOs.

blk_rq_map_sg() Build scatter-gather list.

blk_plug_device() Delay dispatch.

generic_unplug_device() Resume dispatch.

blk_stop_queue() Stop queue when hardware busy.

blk_start_queue() Restart queue.

blk_cleanup_queue() Destroy queue.

============================================================ 8. MENTAL
MODEL ————————————————————

Filesystems generate BIOs.

ll_rw_blk.c transforms those BIOs into optimized hardware requests,
applies hardware limits, cooperates with the I/O scheduler, and
dispatches requests to the block driver.

One-line summary:

ll_rw_blk.c is the central request-management layer of Linux 2.6,
connecting the BIO layer to the I/O scheduler and block drivers while
handling queue management, request merging, dispatch and completion.

