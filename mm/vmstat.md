/*
 * Linux 2.6.20 — mm/vmstat.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (VM statistics subsystem)
 *  - per-CPU counters (performance optimization)
 *  - zone/global stats
 *  - vm_event counters
 *  - stat_threshold batching mechanism
 *  - NUMA statistics
 *
 * Source: user-provided vmstat.c :contentReference[oaicite:0]{index=0}
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
This file is NOT about memory allocation or reclaim.

It is about:

    TRACKING MEMORY STATE

It answers:

    "What is happening in the memory subsystem right now?"
*/


/*
Examples of stats:

    - nr_free_pages
    - nr_active
    - nr_inactive
    - pgfault
    - pgmajfault
    - pgscan / pgsteal
*/


/***************************************************************
 * 1. WHY vmstat EXISTS
 ***************************************************************/

/*
Memory subsystem is highly dynamic:

    allocations
    frees
    reclaim
    swap
    faults

We need:

    LOW OVERHEAD counters
*/


/*
Naive approach:

    global counter with lock → SLOW

Solution:

    per-CPU counters
*/


/***************************************************************
 * 2. PER-CPU VM EVENT COUNTERS (VERY IMPORTANT)
 ***************************************************************/

/*
DEFINE_PER_CPU(struct vm_event_state, vm_event_states)
*/


/*
Each CPU has its own counters:

    vm_event_states[cpu].event[i]
*/


/*
Advantages:

    - no locking
    - cache-friendly
    - scalable
*/


/***************************************************************
 * 3. AGGREGATION (IMPORTANT)
 ***************************************************************/

/*
all_vm_events():

    sum across CPUs
*/


/*
NOTE:

    values are APPROXIMATE
    (may change during summation)
*/


/***************************************************************
 * 4. ZONE COUNTERS (GLOBAL)
 ***************************************************************/

/*
atomic_long_t vm_stat[]
*/


/*
Tracks:

    global + zone-based memory stats
*/


/***************************************************************
 * 5. get_zone_counts()
 ***************************************************************/

/*
Returns:

    total active
    total inactive
    total free
*/


/*
Used by:

    monitoring / /proc interfaces
*/


/***************************************************************
 * 6. PERFORMANCE PROBLEM (CRITICAL)
 ***************************************************************/

/*
Frequent updates to global counters cause:

    cacheline bouncing
    lock contention
*/


/*
Solution:

    per-CPU batching
*/


/***************************************************************
 * 7. vm_stat_diff (PER-CPU DIFF BUFFER)
 ***************************************************************/

/*
Each CPU stores:

    small local diffs (s8 values)
*/


/*
Instead of:

    global += 1

we do:

    local += 1
*/


/***************************************************************
 * 8. stat_threshold (VERY IMPORTANT)
 ***************************************************************/

/*
Each zone has threshold:

    stat_threshold
*/


/*
When local diff exceeds threshold:

    → flush to global counter
*/


/*
This reduces:

    frequency of global updates
*/


/***************************************************************
 * 9. __mod_zone_page_state()
 ***************************************************************/

/*
Core update function.

Flow:

    local += delta

    if abs(local) > threshold:
        flush to global
        reset local
*/


/***************************************************************
 * 10. FAST PATH (INC/DEC OPTIMIZATION)
 ***************************************************************/

/*
__inc_zone_state()
__dec_zone_state()
*/


/*
Optimized:

    avoids full function overhead
    avoids frequent global writes
*/


/***************************************************************
 * 11. WHY s8 (SMALL TYPE)?
 ***************************************************************/

/*
vm_stat_diff uses s8 (8-bit):

    small memory footprint
    encourages batching
*/


/***************************************************************
 * 12. refresh_cpu_vm_stats()
 ***************************************************************/

/*
Flush per-CPU stats → global

Used when:

    reading stats
    CPU hotplug
*/


/***************************************************************
 * 13. refresh_vm_stats()
 ***************************************************************/

/*
Calls refresh_cpu_vm_stats() on all CPUs
*/


/*
Ensures:

    consistent global snapshot
*/


/***************************************************************
 * 14. NUMA STATISTICS
 ***************************************************************/

/*
zone_statistics():

Tracks:

    NUMA_HIT
    NUMA_MISS
    NUMA_LOCAL
    NUMA_OTHER
*/


/*
Used to analyze:

    memory locality
*/


/***************************************************************
 * 15. THRESHOLD CALCULATION (IMPORTANT)
 ***************************************************************/

/*
calculate_threshold():

Depends on:

    - number of CPUs
    - zone size
*/


/*
Goal:

    balance accuracy vs performance
*/


/***************************************************************
 * 16. CPU HOTPLUG SUPPORT
 ***************************************************************/

/*
vm_events_fold_cpu():

    move counters from dead CPU
*/


/*
refresh_zone_stat_thresholds():

    recompute thresholds
*/


/***************************************************************
 * 17. /proc/vmstat (USER INTERFACE)
 ***************************************************************/

/*
Provides:

    pgfault
    pgmajfault
    pgscan
    pgsteal
    etc
*/


/*
Flow:

    read vm_stat + vm_event_states
*/


/***************************************************************
 * 18. CONNECTION WITH vmscan.c
 ***************************************************************/

/*
vmscan.c updates:

    PGSCAN
    PGSTEAL
    PGACTIVATE
*/


/*
vmstat.c records them
*/


/***************************************************************
 * 19. FULL FLOW (END-TO-END)
 ***************************************************************/

/*
Page allocated
    ↓
inc_zone_page_state()

Page reclaimed
    ↓
dec_zone_page_state()

Reclaim events
    ↓
count_vm_events()

User reads /proc/vmstat
    ↓
aggregate per-CPU stats
*/


/***************************************************************
 * 20. KEY INSIGHT (VERY IMPORTANT)
 ***************************************************************/

/*
vmstat.c = "high-performance accounting system"

It ensures:

    fast updates
    low contention
    acceptable accuracy
*/


/***************************************************************
 * 21. INTERVIEW MODEL
 ***************************************************************/

/*
vmstat.c manages VM statistics using per-CPU counters and batching
mechanisms to minimize contention. It aggregates per-CPU values into
global statistics and exposes them via interfaces like /proc/vmstat.
*/


/***************************************************************
 * 22. ONE-LINE SUMMARY
 ***************************************************************/

/*
vmstat.c implements a scalable per-CPU accounting system for memory
statistics with batched updates to global counters.
*/


/***************************************************************
 * END
 ***************************************************************/
