/*
============================================================
FILE: noop-iosched.c (Linux 2.6)
IDE STUDY NOTES (Short)
============================================================

1. PURPOSE
------------------------------------------------------------

The NOOP I/O scheduler is the simplest Linux block scheduler.

It does almost no scheduling policy.

Main idea:

    Keep requests in a simple FIFO list.
    Let the block layer/elevator framework handle merging.
    Dispatch requests with minimal reordering.

NOOP is useful when the storage device already does its own scheduling,
for example:

    - SSDs
    - hardware RAID controllers
    - virtual disks
    - devices with internal command reordering

For such devices, complex kernel-side disk scheduling may only add overhead.

============================================================
2. WHERE NOOP FITS
------------------------------------------------------------

BIO
 |
Request
 |
Elevator framework
 |
NOOP scheduler
 |
Driver
 |
Device

NOOP is an elevator plugin registered with the block elevator framework.

It provides callbacks such as:

    add request
    dispatch request
    queue empty
    merge requests
    init queue
    exit queue

============================================================
3. MAIN DATA STRUCTURE
------------------------------------------------------------

struct noop_data {
        struct list_head queue;
};

This is the entire private scheduler state.

It contains only one list:

    nd->queue

All requests are appended to this list.

Diagram:

    noop_data
       |
       v
    queue:
       rq1 -> rq2 -> rq3 -> rq4

There are no RB trees.
There are no deadlines.
There are no per-process queues.
There are no time slices.

============================================================
4. ADD REQUEST FLOW
------------------------------------------------------------

Function:

    noop_add_request()

Code idea:

    list_add_tail(&rq->queuelist, &nd->queue);

Meaning:

    New request is appended to the tail.

Flow:

    new request
        |
        v
    noop_add_request()
        |
        v
    append to FIFO queue

So requests are kept in arrival order.

============================================================
5. DISPATCH FLOW
------------------------------------------------------------

Function:

    noop_dispatch()

Flow:

    queue empty?
        |
        +-- yes:
        |       return 0
        |
        +-- no:
                take first request
                remove from noop queue
                call elv_dispatch_sort()
                return 1

Important detail:

    NOOP itself uses FIFO,
    but dispatch insertion uses elv_dispatch_sort(q, rq).

So even NOOP may get some final dispatch sorting from the generic
elevator framework.

============================================================
6. MERGE HANDLING
------------------------------------------------------------

Function:

    noop_merged_requests(q, rq, next)

When two requests are merged:

    rq absorbs next

Then NOOP removes next from its queue:

    list_del_init(&next->queuelist);

Why?

    next no longer exists as an independent request.

============================================================
7. FORMER / LATTER REQUEST
------------------------------------------------------------

Functions:

    noop_former_request()
    noop_latter_request()

They return the previous or next request in the NOOP list.

Used by generic elevator/block code for request navigation.

Example:

    rq1 <-> rq2 <-> rq3

former(rq2) = rq1
latter(rq2) = rq3

If at list boundary:

    return NULL

============================================================
8. INIT AND EXIT
------------------------------------------------------------

noop_init_queue()

    allocate noop_data
    initialize request list
    return noop_data

noop_exit_queue()

    verify queue is empty
    free noop_data

Flow:

    elevator framework
        |
        v
    noop_init_queue()
        |
        v
    scheduler private state ready

On removal:

    noop_exit_queue()
        |
        v
    free private state

============================================================
9. REGISTRATION
------------------------------------------------------------

static struct elevator_type elevator_noop

Defines scheduler callbacks:

    elevator_merge_req_fn
    elevator_dispatch_fn
    elevator_add_req_fn
    elevator_queue_empty_fn
    elevator_former_req_fn
    elevator_latter_req_fn
    elevator_init_fn
    elevator_exit_fn

Name:

    "noop"

Module init:

    noop_init()
        |
        v
    elv_register(&elevator_noop)

Module exit:

    noop_exit()
        |
        v
    elv_unregister(&elevator_noop)

============================================================
10. COMPLETE FLOW
------------------------------------------------------------

Request arrives
     |
     v
noop_add_request()
     |
     v
append to nd->queue
     |
     v
block layer asks for dispatch
     |
     v
noop_dispatch()
     |
     v
take first request
     |
     v
elv_dispatch_sort()
     |
     v
driver sees request

============================================================
11. COMPARISON
------------------------------------------------------------

Deadline:

    RB tree + FIFO deadlines.
    Prevents starvation.

CFQ:

    Per-process queues.
    Time-sliced fairness.

Anticipatory:

    Waits briefly for nearby reads.

NOOP:

    Simple FIFO queue.
    Minimal policy.

============================================================
12. MENTAL MODEL
------------------------------------------------------------

NOOP means:

    "Do almost nothing."

It is not completely empty because it still plugs into the generic
elevator framework and supports merging/dispatch callbacks.

But compared to CFQ or Deadline, it is intentionally simple.

One-line summary:

    NOOP is the simplest Linux I/O scheduler: it stores requests in a
    FIFO list, removes merged requests, dispatches the first available
    request, and relies on the block layer/device for most optimization.
*/

