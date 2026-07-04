/*
FILE: elevator.c.notes
SHORT IDE NOTES

1. PURPOSE
- Generic block I/O scheduler framework.
- Does NOT schedule requests itself.
- Provides common infrastructure used by Deadline, CFQ, NOOP, etc.

2. ARCHITECTURE

Filesystem
   |
 BIO
   |
 Request
   |
 elevator.c  <--- framework
   |
 Selected scheduler (Deadline/CFQ/NOOP)
   |
 Driver

3. MAIN RESPONSIBILITIES
- Register schedulers
- Select default scheduler
- Switch scheduler at runtime
- Merge requests
- Maintain request hash
- Maintain RB-tree helpers
- Dispatch requests
- Sysfs interface

4. IMPORTANT OBJECTS

elevator_type
    Scheduler implementation.

elevator_queue
    Per-block-device scheduler instance.

request_queue
    Block device queue.

request
    Individual I/O.

5. REQUEST FLOW

bio
 |
elv_merge()
 |
merge?
 | yes
 v
existing request

no
 |
elv_add_request()
 |
scheduler add callback
 |
dispatch
 |
driver

6. MERGING

elv_rq_merge_ok()
    Basic merge validation.

elv_try_merge()
    Front/back merge detection.

elv_merge()
    Fast cache -> hash -> scheduler callback.

7. HASH TABLE

Purpose:
Fast back-merge lookup.

request end sector
      |
 hash()
      |
 bucket
      |
 matching request

8. RB TREE HELPERS

elv_rb_add()
Insert by sector.

elv_rb_find()
Find exact sector.

elv_rb_del()
Remove.

Schedulers reuse these helpers.

9. DISPATCH

elv_next_request()

dispatch queue empty?
        |
       yes
        |
scheduler dispatch callback
        |
driver

10. SCHEDULER CALLBACKS

elevator_add_req_fn
elevator_merge_fn
elevator_dispatch_fn
elevator_init_fn
elevator_exit_fn
...

Framework calls scheduler-specific code.

11. INITIALIZATION

elevator_init()

choose scheduler
allocate elevator_queue
scheduler init
attach to request_queue

12. SWITCHING

echo deadline > scheduler

|
elv_iosched_store()
|
elevator_switch()
|
drain old scheduler
|
create new scheduler
|
attach new scheduler

13. SYSFS

/queue/scheduler

Shows:
[deadline] cfq noop

Allows runtime switching.

14. REGISTRATION

module_init()
|
elv_register()

Adds scheduler to global list.

15. MENTAL MODEL

elevator.c is the plug-in manager for Linux block schedulers.

Schedulers implement algorithms.
elevator.c provides the common infrastructure.

One-line summary:

elevator.c is the generic block I/O scheduling framework that manages scheduler registration, request merging, dispatch, switching, and common helper infrastructure while delegating scheduling policy to individual schedulers.

