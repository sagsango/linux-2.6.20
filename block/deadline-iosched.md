/*
 * FILE: block/deadline-iosched.c.notes
 *
 * SHORT IDE NOTES
 */

/*
============================================================
1. PURPOSE
============================================================
Deadline scheduler prevents starvation while still keeping
good sequential throughput.

Goals:
 - low read latency
 - bounded request waiting time ("deadline")
 - good elevator ordering
 - simple algorithm

Unlike CFQ:
 - schedules requests, not processes.
*/

/*
============================================================
2. DATA STRUCTURE
============================================================

struct deadline_data

 sort_list[READ/WRITE]
     RB trees ordered by sector.

 fifo_list[READ/WRITE]
     FIFO lists ordered by insertion time.

 next_rq[]
     Cached next request.

 last_sector
     Last dispatched sector.

 batching
     Sequential dispatch count.

 starved
     Number of consecutive read batches.

 Tunables:
     read_expire
     write_expire
     writes_starved
     fifo_batch
     front_merges
*/

/*
============================================================
3. REQUEST STORAGE
============================================================

            request
               |
     +---------+----------+
     |                    |
 RB tree              FIFO list
 by sector         by expire time

RB tree -> throughput
FIFO    -> starvation prevention
*/

/*
============================================================
4. ADD REQUEST
============================================================

deadline_add_request()

 request
   |
   +-> deadline_add_rq_rb()
   |      insert by sector
   |
   +-> rq_set_fifo_time()
   |
   +-> append FIFO list
*/

/*
============================================================
5. REMOVE REQUEST
============================================================

deadline_remove_request()

 remove FIFO
 remove RB tree
 update next_rq cache
*/

/*
============================================================
6. MERGING
============================================================

deadline_merge()
    front merge lookup using RB tree.

deadline_merged_request()
    front merge changes sector,
    so remove/reinsert RB node.

deadline_merged_requests()
    keep earliest deadline,
    delete merged-away request.
*/

/*
============================================================
7. DISPATCH ALGORITHM
============================================================

deadline_dispatch_requests()

if batching still valid
    continue sequential request

else

    reads?

       yes
          if writes starved too long
                 dispatch write
          else
                 dispatch read

    no reads

          dispatch write

Selection:

expired FIFO?
      yes -> oldest FIFO request

      no  -> next_rq cache

      else
           rb_first()

Dispatch:

deadline_move_request()
    |
    +-> update last_sector
    +-> update next_rq
    +-> remove from scheduler
    +-> add to dispatch queue
*/

/*
============================================================
8. STARVATION CONTROL
============================================================

writes_starved

Allows reads to win only limited times.

Example:

writes_starved = 2

Read batch
Read batch
Write batch forced
*/

/*
============================================================
9. BATCHING
============================================================

fifo_batch

Continue sequential requests before
reconsidering direction.

Improves throughput by reducing seeks.
*/

/*
============================================================
10. DEADLINES
============================================================

Read deadline:

    HZ/2

Write deadline:

    5*HZ

Reads expire sooner because they are
usually latency sensitive.
*/

/*
============================================================
11. INITIALIZATION
============================================================

deadline_init_queue()

 allocate deadline_data
 init RB trees
 init FIFO lists
 set default tunables
*/

/*
============================================================
12. SYSFS
============================================================

Tunables:

 read_expire
 write_expire
 writes_starved
 front_merges
 fifo_batch
*/

/*
============================================================
13. REGISTRATION
============================================================

module_init()
      |
      v
deadline_init()
      |
      v
elv_register()

Scheduler name:

    "deadline"
*/

/*
============================================================
14. COMPLETE FLOW
============================================================

bio
 |
request
 |
deadline_add_request()
 |
RB tree + FIFO
 |
deadline_dispatch_requests()
 |
deadline_move_request()
 |
driver
 |
completion
*/

/*
============================================================
15. MENTAL MODEL
============================================================

RB tree
    -> good seek pattern

FIFO
    -> prevent starvation

Deadline
    -> every request eventually runs

One-line summary:

Deadline is an elevator scheduler that
dispatches requests primarily in sector
order while enforcing expiration times so
reads stay responsive and writes never
starve.
*/

