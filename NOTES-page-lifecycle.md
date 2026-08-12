# Question 1: zone, page relationship, hot cold pages, (active inactive list) how the page get recyceled, and swaped etc in linux kernel version 2.6.20; with flow diagram (ASCII)
Yes. Linux **2.6.20** uses a fairly classic **zone + LRU-based page reclaim** design. The easiest way to understand it is:

> **Node → Zone → Page → LRU list → Active/Inactive → Reclaim → Free or Swap/File writeback**

The important distinction is that **“zone” is a physical-memory organization**, while **active/inactive is a reclaim-policy organization**.

The explanation below is specifically for the **Linux 2.6.20-era VM**, not today's Linux VM. The 2.6.20 source tree contains `mm/vmscan.c`, `mm/page_alloc.c`, `mm/swap.c`, `mm/swap_state.c`, etc. ([Kernel][1])

---

# 1. Big picture

Think of physical memory as:

```text
                         SYSTEM RAM
                            |
                 +----------+----------+
                 |                     |
               NUMA node 0          NUMA node 1
                 |                     |
          +------+------+       +------+------+
          |             |       |             |
       ZONE_DMA      ZONE_NORMAL ...      ZONE_NORMAL
          |
       struct zone
          |
   +------+----------------------+
   |                             |
active_list                 inactive_list
   |                             |
 hot/recently used          cold/not recently used
   |                             |
   +-------------+---------------+
                 |
              reclaim
                 |
       +---------+----------+
       |                    |
   page is clean        page is dirty/
       |                anonymous
       |                    |
       v                    v
     FREE              writeback / SWAP
```

In 2.6.20, `struct zone` contains the zone's LRU lists, including `active_list` and `inactive_list`. Reclaim code in `mm/vmscan.c` scans these lists to find pages that can be reclaimed. The source history around 2.6.20 also shows `isolate_lru_pages()` operating directly on `zone->active_list` and `zone->inactive_list`. ([Mail Archive][2])

---

# 2. What is a `zone`?

A **zone** is a region of physical memory.

It is **not** the same thing as a process's memory or a virtual-memory region.

Historically, Linux divides physical RAM into zones because some physical pages have different allocation constraints.

Typical 32-bit x86 example:

```text
Physical RAM
0
+------------------------------------------------+
|                  ZONE_DMA                      |
|        memory usable by DMA-limited devices   |
+------------------------------------------------+
|                ZONE_NORMAL                    |
|       directly accessible kernel memory       |
+------------------------------------------------+
|                ZONE_HIGHMEM                   |
|       high physical memory                    |
+------------------------------------------------+
MAX
```

The exact zones depend on architecture/configuration.

For example:

```text
ZONE_DMA
ZONE_NORMAL
ZONE_HIGHMEM
```

On a machine without highmem, you might effectively have:

```text
ZONE_DMA
ZONE_NORMAL
```

The key point:

```text
zone = physical-memory allocation/reclaim unit
```

not:

```text
zone = "hot pages"
```

and not:

```text
zone = process address space
```

---

# 3. Relationship between `struct page` and `struct zone`

Every physical page frame that Linux manages has a `struct page`.

Conceptually:

```text
Physical RAM

+---------+---------+---------+---------+---------+
| Page 0  | Page 1  | Page 2  | Page 3  | Page 4  |
+---------+---------+---------+---------+---------+
     |         |         |         |
     v         v         v         v
 struct page  struct page ...
```

Linux maintains metadata for each physical page:

```text
struct page
+----------------------+
| flags                |
| count/refcount       |
| mapping              |
| index                |
| virtual              |
| lru                  |----+
| ...                  |    |
+----------------------+    |
                            |
                            v
                       LRU list
```

The `lru` member is important here.

It allows a page to participate in an LRU list.

For example:

```text
zone->active_list
       |
       +--> page A
       |
       +--> page B
       |
       +--> page C


zone->inactive_list
       |
       +--> page X
       |
       +--> page Y
       |
       +--> page Z
```

The 2.6.20 reclaim implementation explicitly isolates pages from `zone->inactive_list` and `zone->active_list` while holding the zone's LRU lock. ([Mail Archive][2])

---

# 4. So what does a zone actually contain?

Simplified:

```text
struct zone
{
        free pages
        watermarks
        allocation information

        active_list
        inactive_list

        lru_lock

        page counters

        ...
}
```

Conceptually:

```text
                  struct zone
                       |
       +---------------+----------------+
       |               |                |
       v               v                v
   free pages      active_list     inactive_list
                       |                |
                       |                |
                  +----+----+       +---+---+
                  |    |    |       |   |   |
                 P1   P2   P3      P7  P8  P9
```

This is the first important relationship:

```text
ZONE
 |
 +-- free-page management
 |
 +-- active LRU
 |
 +-- inactive LRU
 |
 +-- watermarks
 |
 +-- reclaim/accounting information
```

---

# 5. What is an LRU list?

LRU means:

**Least Recently Used**

The VM wants to identify pages that haven't been used recently.

Very simplified:

```text
recently used                          less recently used
     |                                        |
     v                                        v

   HOT ------------------------------------> COLD

   Active list --------------------------> Inactive list
```

A page is not permanently "hot" or "cold."

Its classification can change.

For example:

```text
        page is accessed
              |
              v
        +-------------+
        | ACTIVE/HOT  |
        +-------------+
              |
       no recent use
              |
              v
       +--------------+
       | INACTIVE/COLD|
       +--------------+
              |
          reclaim
              |
              v
         free / swap
```

---

# 6. Important: Active does NOT simply mean "currently being used"

This is a common misunderstanding.

In Linux 2.6.20, the active/inactive mechanism is an **approximation of recent use**.

The CPU/MMU has an **accessed/young bit** in page-table entries.

Conceptually:

```text
CPU accesses page
       |
       v
MMU sets "accessed" bit
       |
       v
Linux later examines page usage
       |
       +---- recently accessed ---> keep/promote
       |
       +---- not accessed --------> candidate for inactive/reclaim
```

So:

```text
ACTIVE ≈ recently used
INACTIVE ≈ not recently used
```

rather than:

```text
ACTIVE = currently executing
INACTIVE = unused forever
```

---

# 7. Active and inactive lists

In the 2.6.20 VM, each zone has lists along these lines:

```text
                    ZONE
                     |
              +------+------+
              |             |
              v             v
        active_list    inactive_list
              |             |
        +-----+-----+   +---+---+
        |     |     |   |   |   |
       P1    P2    P3  P4  P5  P6
```

The reclaim code normally concentrates on the **inactive list**.

Why?

Because the kernel assumes:

```text
inactive page
      ↓
probably not needed soon
      ↓
good reclaim candidate
```

while:

```text
active page
      ↓
recently used
      ↓
don't immediately reclaim
```

---

# 8. But there is another important dimension: file vs anonymous

This is where the terminology becomes confusing.

Pages can broadly be:

```text
                    PAGE
                     |
             +-------+-------+
             |               |
          FILE             ANON
             |               |
       page cache         process memory
       executable         malloc/heap
       mapped file       stack, etc.
```

And each can be active/inactive.

Conceptually:

```text
                 LRU
                  |
       +----------+----------+
       |                     |
    ACTIVE                 INACTIVE
       |                     |
   +---+---+             +---+---+
   |       |             |       |
 file     anon          file    anon
```

So you can think of four logical categories:

```text
active file
active anonymous

inactive file
inactive anonymous
```

However, **Linux 2.6.20's implementation/details differ from later kernels**, so don't blindly apply today's `LRU_ACTIVE_FILE`, `LRU_INACTIVE_ANON`, memcg LRU terminology to 2.6.20.

The basic reclaim idea is nevertheless the same.

---

# 9. What happens when memory becomes low?

This is where `kswapd` and direct reclaim enter.

Suppose an application asks for memory:

```text
        process
           |
           | allocate page
           v
     page allocator
           |
           v
    Is enough free RAM?
       /          \
     YES           NO
      |             |
      v             v
 allocate       reclaim
                   |
             +-----+-----+
             |           |
          kswapd     direct reclaim
```

Linux maintains zone watermarks:

```text
          HIGH
           |
           |  kswapd tries to reach this
           |
          LOW
           |
           | reclaim pressure
           |
          MIN
           |
           | serious shortage
           v
```

Conceptually:

```text
free memory
    |
    |      HIGH
    |-------|----------------
    |      /
    |     /   normal
    |----| LOW
    |    /
    |---| MIN
    |
    +------------------------> time
```

`kswapd` is the background reclaim thread.

Direct reclaim can also happen in the context of the allocating process.

---

# 10. `kswapd`

Think of `kswapd` as:

> "Keep enough free pages available so future allocations don't immediately fail."

Flow:

```text
                free memory falls
                       |
                       v
                 wakeup kswapd
                       |
                       v
                 kswapd wakes
                       |
                       v
                 scan zones
                       |
                       v
                reclaim pages
                       |
                       v
                free pages rise
                       |
                       v
               reach HIGH watermark
                       |
                       v
                  sleep again
```

The 2.6.x reclaim implementation has `balance_pgdat()` as an important part of this process; the source commentary says that `balance_pgdat()` works across the node's zones until they reach their high watermarks. ([Mail Archive][2])

---

# 11. The reclaim path

A useful simplified 2.6.20 call-flow is:

```text
kswapd
  |
  v
balance_pgdat()
  |
  v
shrink_zones()
  |
  v
shrink_zone()
  |
  +----------------------+
  |                      |
  v                      v
inactive list          active list
  |                      |
  v                      v
shrink_inactive_list  scan/reconsider
  |
  v
isolate_lru_pages()
  |
  v
page list temporarily isolated
  |
  v
page_referenced()
 /        \
used       not used
 |             |
 v             v
reactivate     reclaim
               |
        +------+------+
        |             |
       FILE          ANON
        |             |
     clean?       can swap?
      /  \           / \
    yes  no        yes  no
     |    |         |    |
     v    v         v    v
   free writeback  swap  keep/
                       retry
```

The 2.6.20 source shows `shrink_inactive_list()` isolating pages from the inactive list and then reclaiming them; the same source also contains active-list isolation when the scanner needs to reconsider active pages. ([Mail Archive][2])

---

# 12. What does "isolate" mean?

This is an important detail.

The kernel doesn't necessarily immediately delete a page from the system when it decides to reclaim it.

It temporarily removes it from the LRU list:

```text
inactive_list

P1 -> P2 -> P3 -> P4 -> P5
             |
             | isolate
             v

inactive_list:

P1 -> P2 -> P4 -> P5


temporary page_list:

P3
```

This is useful because reclaim can now examine the page without it being simultaneously treated as a normal LRU member.

The 2.6.20-era code calls this `isolate_lru_pages()`. ([Mail Archive][2])

---

# 13. What happens to an inactive page?

Suppose:

```text
inactive_list

P1 -> P2 -> P3 -> P4 -> P5
```

The scanner takes some pages:

```text
              isolate
                 |
                 v

P1 P2 P3 P4 P5 ---> [P1 P2 P3]
                       |
                       v
                  examine pages
```

For each page, the kernel asks essentially:

```text
Can I reclaim this page?
```

There are several possibilities.

---

# 14. Case 1 — Clean file-backed page

Suppose:

```text
P1 = clean page-cache page
```

The data already exists on disk.

Therefore:

```text
RAM:
+----------------+
| P1 = file data |
+----------------+

Disk:
+----------------+
| same file data |
+----------------+
```

There is no need to write P1 back.

So:

```text
P1
 |
 | clean file page
 v
FREE
```

This is one of the cheapest forms of reclaim.

```text
       clean file page
              |
              v
        remove from LRU
              |
              v
           free page
```

---

# 15. Case 2 — Dirty file-backed page

Suppose:

```text
P2 = file-backed + dirty
```

RAM contains newer data than the disk:

```text
RAM                         DISK

+----------+                +----------+
| new data |                | old data |
+----------+                +----------+
      P2
```

It cannot simply be discarded.

Instead:

```text
P2
 |
 | dirty file page
 v
writeback
 |
 v
disk updated
 |
 v
page can eventually be reclaimed
```

So:

```text
dirty file page
       |
       v
  write to disk
       |
       v
 clean
       |
       v
  reclaim/free
```

---

# 16. Case 3 — Anonymous page

Now consider:

```text
malloc()
heap
stack
anonymous mmap()
```

There may be no filesystem copy.

Example:

```text
process virtual memory

0x1000 -> anonymous page P
```

RAM:

```text
P = "hello"
```

There is no normal file containing that page.

If memory pressure occurs, Linux can use **swap**.

```text
RAM                     SWAP

+---------+             +---------+
| P       | ----------> | slot X  |
| "hello" |   swapout   | "hello" |
+---------+             +---------+
```

After successful swap-out:

```text
RAM
 |
 P removed
 |
 v
free physical page
```

The page-table representation is changed so that a future access causes a page fault and the page can be read back from swap.

---

# 17. Swap is NOT the same thing as page reclaim

This distinction is extremely important.

People often say:

> "Linux swaps pages when RAM is full."

More accurately:

```text
                 memory pressure
                       |
                       v
                    reclaim
                       |
          +------------+------------+
          |                         |
     file-backed                anonymous
          |                         |
          v                         v
    discard if clean             swap out
    write if dirty
```

So:

```text
RECLAIM
   |
   +---- clean page-cache page ---> discard
   |
   +---- dirty file page ---------> writeback
   |
   +---- anonymous page ----------> swap
```

**Swap is one possible mechanism used to reclaim anonymous memory.**

---

# 18. What makes a page "hot"?

Imagine:

```text
P1: accessed constantly
P2: accessed occasionally
P3: hasn't been accessed
```

Conceptually:

```text
             access frequency
                    |
                    v

P1  ███████████████████  very hot
P2  ███████              moderately hot
P3  █                    cold
```

The kernel uses page-reference information to make an approximation.

For example:

```text
CPU accesses P1
      |
      v
accessed/young bit becomes set
      |
      v
VM scanner examines P1
      |
      v
"recently referenced"
      |
      v
keep active
```

Whereas:

```text
P3 hasn't been accessed
       |
       v
scanner checks it
       |
       v
not recently referenced
       |
       v
good inactive/reclaim candidate
```

---

# 19. Active → inactive

A simplified picture:

```text
                  PAGE
                    |
                    v
              active/hot
                    |
             no recent use
                    |
                    v
            inactive/cold
                    |
             reclaim scan
                    |
          +---------+---------+
          |                   |
       referenced         not referenced
          |                   |
          v                   v
       ACTIVE             RECLAIM
```

But don't interpret this as a simple timer:

```text
active for 10 seconds
        ↓
inactive
```

It is reference-based and tied to the VM's scanning/reclaim algorithm.

---

# 20. Inactive → active

A cold page can become hot again.

Example:

```text
             inactive
                |
                v
         process accesses it
                |
                v
         page is referenced
                |
                v
            reactivate
                |
                v
             active
```

So the LRU isn't a one-way pipeline.

It's more like:

```text
                   +----------------+
                   |                |
                   v                |
               ACTIVE               |
                   |                |
             becomes cold           |
                   |                |
                   v                |
              INACTIVE              |
                   |                |
            accessed again ----------+
                   |
             not referenced
                   |
                   v
                RECLAIM
```

This is why **"inactive" does not mean "dead."**

It means:

> "This page currently looks like a better reclaim candidate."

---

# 21. The complete page life cycle

Here's the useful mental model:

```text
                         PAGE ALLOCATION
                               |
                               v
                        +--------------+
                        | Physical RAM |
                        +--------------+
                               |
                               v
                         page becomes
                           accessible
                               |
                               v
                         +-------------+
                         |   ACTIVE    |
                         |   / HOT     |
                         +-------------+
                               |
                      less recently used
                               |
                               v
                         +-------------+
                         |  INACTIVE   |
                         |  / COLD     |
                         +-------------+
                               |
                         reclaim scan
                               |
                +--------------+--------------+
                |              |              |
                v              v              v
             clean file    dirty file       anon
                |              |              |
                v              v              v
              FREE          writeback       swap
                               |              |
                               v              v
                             FREE           FREE
                                              |
                                              |
                                      later page fault
                                              |
                                              v
                                           swap-in
                                              |
                                              v
                                             RAM
                                              |
                                              v
                                           ACTIVE
```

That is the core of 2.6.20 page recycling.

---

# 22. "Recycled" page — what does that really mean?

Suppose physical page frame `PFN 12345` is currently used by process A:

```text
PFN 12345
   |
   v
Page A
```

Reclaim happens:

```text
Page A
  |
  v
reclaim
  |
  v
physical frame becomes FREE
```

Then process B allocates memory:

```text
              PFN 12345
                  |
                  v
              Page B
```

So the physical frame was "recycled":

```text
             same physical frame
                    |
        +-----------+-----------+
        |                       |
        v                       v
     Process A              Process B
       page                    page
```

The **physical page frame** is reused; the old logical contents are no longer associated with it.

---

# 23. "Swapped out" versus "recycled"

These are different stages.

Suppose anonymous page A is:

```text
RAM
 |
 +--> Page A
```

Reclaim:

```text
Page A
  |
  | swap out
  v
SWAP SLOT #42
```

Physical RAM page:

```text
             Page A
               |
               v
          swap slot #42
               |
               v
      physical frame FREE
```

Then another process can use the physical frame:

```text
SWAP SLOT #42                RAM
     |                         |
     |                         |
 Page A                    Page B
     |                         |
     +----> future fault       |
              |                |
              v                |
           swap-in             |
              |                |
              +----------------+
```

So:

```text
swap-out
    ↓
free physical frame
    ↓
frame recycled for another page
```

---

# 24. What does "swapped in" mean?

Later the process accesses the missing page:

```text
process accesses virtual address
             |
             v
         page fault
             |
             v
    page is not in RAM
             |
             v
     page is in swap?
             |
             v
        read from swap
             |
             v
     allocate free frame
             |
             v
       read page into RAM
             |
             v
     update page tables
             |
             v
       restart instruction
```

ASCII:

```text
CPU
 |
 | access virtual address
 v
Page table
 |
 | not-present / swap entry
 v
PAGE FAULT
 |
 v
find page in SWAP
 |
 v
allocate physical page
 |
 v
read swap slot
 |
 v
+------------------+
| physical page    |
| restored         |
+------------------+
 |
 v
update PTE
 |
 v
resume process
```

---

# 25. File page versus anonymous page

This comparison is worth memorizing:

| Page type          | Reclaim action                      |
| ------------------ | ----------------------------------- |
| Clean file-backed  | Discard; reload from file if needed |
| Dirty file-backed  | Write back, then reclaim            |
| Anonymous          | Usually swap out if eligible        |
| Unevictable/locked | Don't reclaim normally              |

For example:

```text
FILE PAGE

RAM                    FILE
 |                       |
 +------ same data ------+
          |
       clean
          |
          v
       discard
```

versus:

```text
ANONYMOUS PAGE

RAM                    SWAP
 |                       |
 +---- data ------------>+
          |
       swap out
          |
          v
    physical frame
        FREE
```

---

# 26. Where does the page live while being reclaimed?

This is an important implementation detail.

Initially:

```text
zone->inactive_list
        |
        +--> P1
        +--> P2
        +--> P3
```

Scanner isolates some:

```text
zone->inactive_list
        |
        +--> P1
        +--> P2


temporary list
        |
        +--> P3
```

Then reclaim examines P3.

Possible outcomes:

```text
                         P3
                         |
                 +-------+-------+
                 |               |
              reclaim          keep
                 |               |
        +--------+--------+      v
        |                 |   active /
       free             swap  inactive
```

The exact list transitions depend on the page type and reference/reclaim result.

---

# 27. Why scan the active list at all?

You might ask:

> If active means hot, why touch it?

Because active/inactive is only an approximation.

A page can have been active earlier but become cold.

So reclaim periodically examines active pages.

Conceptually:

```text
ACTIVE
  |
  | scanner examines references
  |
  +---- referenced ------> stay ACTIVE
  |
  +---- not referenced --> move toward INACTIVE
```

This prevents an old "hot" classification from permanently protecting a page.

The 2.6.20 code has an active-list scanning path that calls `isolate_lru_pages()` on `zone->active_list`. ([Mail Archive][2])

---

# 28. A more accurate flow diagram

Here is the complete picture I would use when studying the 2.6.20 VM:

```text
                         +----------------------+
                         |      PROCESS         |
                         |  malloc()/mmap()/IO  |
                         +----------+-----------+
                                    |
                                    v
                           PAGE ALLOCATION
                                    |
                                    v
                       +------------------------+
                       |       ZONE             |
                       |                        |
                       | free pages             |
                       | watermarks              |
                       | active_list             |
                       | inactive_list           |
                       +-----------+------------+
                                   |
                      +------------+-------------+
                      |                          |
                      v                          v
               ACTIVE / HOT                INACTIVE / COLD
                      |                          |
                      |                          |
               referenced?                 reclaim scan
                      |                          |
             +--------+--------+          +------+------+
             |                 |          |             |
            YES               NO         file          anon
             |                 |          |             |
             v                 v          |             |
          ACTIVE          INACTIVE        |        swap eligible?
                                             |        /       \
                                             |      YES       NO
                                             |       |          |
                                             |       v          v
                                             |     SWAP      keep/retry
                                             |
                                      +------+------+
                                      |             |
                                    clean          dirty
                                      |             |
                                      v             v
                                    FREE         WRITEBACK
                                                    |
                                                    v
                                                   FREE
```

---

# 29. Where `kswapd` fits

Now put the background daemon into that picture:

```text
                 +------------------+
                 |    page_alloc    |
                 +--------+---------+
                          |
                  free pages too low
                          |
                          v
                  wakeup kswapd
                          |
                          v
                 +----------------+
                 |     kswapd     |
                 +-------+--------+
                         |
                         v
                  balance_pgdat()
                         |
                         v
                   shrink_zones()
                         |
                         v
                    shrink_zone()
                         |
                         v
              +----------+----------+
              |                     |
              v                     v
       active/inactive LRU      slab caches
              |
              v
      shrink_inactive_list()
              |
              v
       isolate_lru_pages()
              |
              v
           reclaim
              |
       +------+------+
       |             |
       v             v
   file pages     anon pages
       |             |
       v             v
 writeback/free     swap
       |             |
       +------+------+
              |
              v
        FREE PAGES
              |
              v
       watermarks OK
              |
              v
         kswapd sleeps
```

The old 2.6.x implementation explicitly has `balance_pgdat()` driving zone balancing and `shrink_inactive_list()` doing the inactive-LRU reclaim work. ([Mail Archive][2])

---

# 30. Direct reclaim

There is another path:

```text
Process A
   |
   | malloc/page allocation
   v
allocator
   |
   | insufficient free pages
   v
DIRECT RECLAIM
   |
   v
try_to_free_pages()
   |
   v
shrink_zones()
   |
   v
reclaim
```

So you can have:

```text
                  MEMORY PRESSURE
                       |
            +----------+----------+
            |                     |
            v                     v
         kswapd              allocating task
            |                     |
            v                     v
       background             direct reclaim
         reclaim
```

The 2.6.20 reclaim code has `try_to_free_pages()` as the synchronous reclaim path, while `balance_pgdat()` is used by `kswapd`. ([Mail Archive][2])

---

# 31. Watermarks explain when reclaim happens

Think of each zone as having:

```text
              zone
               |
        +------+------+
        |             |
      free          used
        |
        v
+---------------------------+
| HIGH                      |
|                           |
| LOW                       |
|                           |
| MIN                       |
+---------------------------+
```

When free pages are comfortably above the high watermark:

```text
free > HIGH

       no serious reclaim
```

When free memory falls:

```text
free < LOW

       reclaim pressure
```

When it gets very low:

```text
free < MIN

       aggressive reclaim /
       allocation trouble
```

The precise allocator/kswapd behavior is more nuanced than this diagram, but this is the right mental model.

---

# 32. Zone vs LRU: the key distinction

This is probably the most important conceptual point in your question.

### Zone answers:

> **Where is this physical page located / from which physical-memory pool can it be allocated?**

Example:

```text
Page P
  |
  +----> ZONE_NORMAL
```

### Active/inactive answers:

> **How good a reclaim candidate is this page?**

Example:

```text
Page P
  |
  +----> ZONE_NORMAL
  |
  +----> ACTIVE
```

So a page has both dimensions:

```text
                 PAGE
                  |
          +-------+-------+
          |               |
       physical         reclaim
       organization     status
          |               |
          v               v
        ZONE          ACTIVE/INACTIVE
```

---

# 33. A concrete example

Suppose you have:

```text
4 GB RAM
```

and imagine:

```text
ZONE_NORMAL
|
+-- P1  active
+-- P2  active
+-- P3  active
+-- P4  inactive
+-- P5  inactive
+-- P6  inactive
+-- ...
```

An application needs 100 MB.

Free pages aren't sufficient.

Then:

```text
allocation
    |
    v
free pages insufficient
    |
    v
reclaim
    |
    v
inactive pages scanned
```

Suppose:

```text
P4 = clean file cache
P5 = dirty file cache
P6 = anonymous
```

Then:

```text
P4 ---> discard ---> FREE

P5 ---> writeback ---> FREE

P6 ---> swap out ---> FREE
```

Now:

```text
                    BEFORE

ZONE_NORMAL
 |
 +-- active
 |
 +-- inactive
 |     |
 |     +-- P4
 |     +-- P5
 |     +-- P6
 |
 +-- free = low


                    AFTER

ZONE_NORMAL
 |
 +-- active
 |
 +-- inactive
 |
 +-- free
       |
       +-- old P4 frame
       +-- old P5 frame
       +-- old P6 frame
```

The freed physical frames can now satisfy the allocation.

---

# 34. And then those frames get reused

Suppose P4's physical frame is:

```text
PFN 1000
```

After reclaim:

```text
PFN 1000
   |
   v
FREE
```

New allocation:

```text
PFN 1000
   |
   v
New Page Q
```

Then Q becomes active after being used:

```text
FREE
  |
  v
allocated
  |
  v
used
  |
  v
ACTIVE
  |
  v
INACTIVE
  |
  v
reclaim
```

So a physical frame can go through many different logical pages over its lifetime.

---

# 35. One subtle point: page cache and swap cache

In Linux 2.6.20 there is also a **swap cache** mechanism.

Conceptually:

```text
Anonymous page
      |
      v
    swapout
      |
      v
 swap cache + swap slot
      |
      v
physical page can be reclaimed
```

The swap cache helps coordinate swap I/O and page faults so that the kernel can manage the relationship between the in-memory page and its swap slot.

This is one reason the actual kernel implementation is more complicated than the simplified:

```text
RAM -> SWAP
```

picture.

---

# 36. Another subtle point: not every page can be reclaimed

Some pages are not ordinary reclaim candidates.

Examples include pages that are:

```text
locked
kernel-critical
unevictable
under certain I/O operations
etc.
```

So the real reclaim algorithm is closer to:

```text
             candidate page
                   |
                   v
             Can reclaim?
              /       \
            YES        NO
             |          |
             v          v
          reclaim     keep/skip
```

This is why you cannot simply say:

> "When memory is low, Linux moves every active page to inactive and swaps it."

It doesn't.

---

# 37. The 2.6.20 source-level map

If you're reading the kernel source, these are the important areas:

```text
mm/
 |
 +-- page_alloc.c
 |      |
 |      +-- physical page allocation
 |      +-- free pages
 |      +-- watermarks
 |
 +-- vmscan.c
 |      |
 |      +-- kswapd
 |      +-- balance_pgdat()
 |      +-- shrink_zones()
 |      +-- shrink_zone()
 |      +-- shrink_inactive_list()
 |      +-- reclaim
 |
 +-- swap.c
 |      |
 |      +-- page/LRU-related operations
 |
 +-- swap_state.c
 |      |
 |      +-- swap cache
 |
 +-- page_io.c
        |
        +-- page I/O
```

The 2.6.20 source tree indeed contains these VM files, including `page_alloc.c`, `swap.c`, `swap_state.c`, and `vmscan.c`. ([Kernel][1])

---

# 38. The most useful mental model

If you're preparing for a Linux kernel/VM interview or debugging memory behavior, remember this:

```text
                         PHYSICAL RAM
                              |
                         +----+----+
                         |         |
                       NODE       NODE
                         |
                       ZONES
                         |
                +--------+--------+
                |                 |
           ACTIVE LRU        INACTIVE LRU
                |                 |
              HOT-ish           COLD-ish
                |                 |
                |             reclaim scan
                |                 |
                |       +---------+---------+
                |       |                   |
                |     FILE                ANON
                |       |                   |
                |   +---+---+            SWAP
                |   |       |
                | clean   dirty
                |   |       |
                | free   writeback
                |           |
                +-----------+
                            |
                           FREE
                            |
                            v
                       new allocation
```

And the **complete lifecycle** is:

```text
                  allocation
                      |
                      v
                   FREE
                      |
                      v
                  allocated
                      |
                      v
                 ACTIVE/HOT
                      |
               less recently used
                      |
                      v
               INACTIVE/COLD
                      |
                 reclaim scan
                      |
          +-----------+-----------+
          |                       |
      reclaimable             referenced
          |                       |
          v                       v
   +------+------+              ACTIVE
   |             |
 clean file    anon
   |             |
   v             v
 FREE          SWAP
                 |
                 v
              FREE
                 |
                 v
          physical frame reused
                 |
                 v
            new allocation
```

### In one sentence

**In Linux 2.6.20, a physical page belongs to a memory `zone`, and reclaim tracks reclaimable pages using the zone's active/inactive LRU lists; pages that appear cold migrate toward the inactive list, where `kswapd` or direct reclaim can free clean file pages, write back dirty file pages, or swap anonymous pages, thereby returning their physical frames to the zone's free pool for reuse.** ([Mail Archive][2])

One caution: modern Linux has substantially changed the LRU internals and terminology, so when studying **2.6.20**, use the 2.6.20 source rather than current `mm/vmscan.c`; current source, for example, has folios, memcg LRUs, and additional reclaim concepts that didn't exist in this form in 2.6.20. ([github.com][3])

[1]: https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/%2B/refs/tags/v2.6.20-rc5/mm/?utm_source=chatgpt.com "mm - pub/scm/linux/kernel/git/torvalds/linux - Git at Google"
[2]: https://www.mail-archive.com/devel%40openvz.org/msg01248.html?utm_source=chatgpt.com "[Devel] [PATCH 7/8] Page scanner changes needed to implement per-container scanner"
[3]: https://github.com/torvalds/linux/blob/master/mm/vmscan.c?utm_source=chatgpt.com "linux/mm/vmscan.c at master · torvalds/linux · GitHub"




# Question 2: ative inactive list tranzition when and how it happens, and page->_count. with example (one more doubt lets say page fault happens we did lazy mmap, so attach one page frame, and now it will be accessed frequently then cpu will just keep setting the accessed bit and no page fault will happen  then does kernel do anything about it because it does not know it was accessed 100 time, as no page fault ouurced)
Yes — your **page-fault/accessed-bit question is exactly the right question** for understanding how the Linux 2.6.20 LRU works.

There are really **three separate mechanisms** to keep straight:

1. `ACTIVE ↔ INACTIVE` — reclaim policy.
2. `PTE accessed/young bit` — hardware records that a virtual mapping was accessed.
3. `page->_count` — **reference count/lifetime ownership**, not an access counter.

Let's go through them with one concrete example.

---

# 1. First: `page->_count` is NOT "how many times page was accessed"

This is the first thing to correct.

Suppose:

```text
struct page *page = physical page PFN 1000
```

and:

```text
page->_count = 1
```

That does **not** mean:

> CPU accessed this page once.

It means, roughly:

> There is one reference keeping this `struct page`/physical page from being freed.

Think of `_count` as a **lifetime reference count**.

Conceptually:

```text
             page->_count
                  |
        +---------+---------+
        |         |         |
        v         v         v
     mapping    temporary  other
     reference  kernel     references
                reference
```

The exact sources of references depend on what the kernel is doing with the page.

---

# 2. Access frequency is recorded differently

Suppose:

```text
Virtual address 0x400000
        |
        v
       PTE
        |
        v
Physical page PFN 1000
```

The PTE has an **accessed/young bit**.

Conceptually:

```text
PTE:

+--------------------------------+
| PFN | R/W | USER | ACCESSED | ... |
+--------------------------------+
                    ^
                    |
                  CPU/MMU
```

When the CPU accesses the mapping:

```text
CPU load/store
      |
      v
PTE accessed bit = 1
```

The CPU does **not** increment:

```text
page->_count
```

And it does not generate a page fault every time.

That's the key.

---

# 3. Your example

You said:

> lazy mmap → page fault → attach one page frame → now accessed frequently → CPU keeps setting accessed bit → no page fault → does kernel do anything?

**Yes.**

The kernel doesn't need a page fault for every access.

Instead, later, when the VM needs to make reclaim decisions, it **checks the page-table accessed/reference information**.

So:

```text
                 CPU
                  |
            accesses page
                  |
                  v
          +---------------+
          | PTE accessed=1|
          +---------------+
                  |
                  |
             NO page fault
                  |
                  |
          later VM scanning
                  |
                  v
          kernel examines
          reference state
                  |
                  v
       "this page was recently used"
                  |
                  v
          keep / activate
```

That's how the kernel learns about accesses **without trapping on every access**.

---

# 4. Why doesn't the CPU count 100 accesses?

Because that would be unnecessary.

Suppose:

```text
Page P
```

is accessed:

```text
1st access  -> accessed = 1
2nd access  -> accessed = 1
3rd access  -> accessed = 1
...
100th access -> accessed = 1
```

The bit is essentially:

```text
          "Was this accessed?"
                    |
                    v
                   YES
```

not:

```text
          "How many times?"
                    |
                    v
                  100
```

The VM usually only needs to know:

> **Was this page used since the last time I checked?**

That's enough for an LRU approximation.

---

# 5. Think of it as sampling

This is a very good mental model.

Imagine the page is being accessed continuously:

```text
CPU accesses:

| | | | | | | | | | | | | | | | | |
```

Hardware records:

```text
accessed = 1
```

The kernel doesn't watch every access.

Instead, periodically:

```text
             time
              |
              v

CPU:       x x x x x x x x x x x x
                         |
                         |
Kernel checks            |
                         v
                    accessed = 1
                         |
                         v
                    clear/check
                         |
                         v
                  continue scanning
```

So Linux is doing **sampling**, not counting.

---

# 6. Now let's connect this to ACTIVE/INACTIVE

Suppose our page starts here:

```text
ZONE_NORMAL
   |
   +-- active_list
   |      |
   |      +---- Page P
   |
   +-- inactive_list
```

Page P is active.

Eventually memory pressure occurs.

The VM scans some active pages.

Conceptually:

```text
active_list
     |
     +---- P
     |
     v
scan P
     |
     v
is P recently referenced?
```

The kernel examines the page's mappings/accessed state.

Two possibilities:

### Case A: accessed

```text
PTE accessed = 1
        |
        v
recently used
        |
        v
keep active
```

### Case B: not accessed

```text
PTE accessed = 0
        |
        v
not recently used
        |
        v
move toward inactive
```

So:

```text
                  ACTIVE
                     |
               VM scans page
                     |
              +------+------+
              |             |
          referenced     not referenced
              |             |
              v             v
           ACTIVE        INACTIVE
```

The 2.6.x `vmscan.c` code explicitly describes the active-list scan as moving pages to inactive when they aren't referenced, while referenced pages can be rotated/kept. ([Kernel][1])

---

# 7. Now the really important part: inactive → active

Suppose the page eventually gets moved to inactive:

```text
active_list:

A -> B -> C

inactive_list:

D -> E -> P -> F
```

Now the application accesses P.

The CPU does:

```text
PTE.accessed = 1
```

No page fault.

Then later reclaim scans P:

```text
inactive_list
      |
      v
      P
      |
      v
page_referenced(P)
      |
      v
PTE says "referenced"
      |
      v
P is considered recently used
      |
      v
ACTIVE / kept from reclaim
```

Conceptually:

```text
                 INACTIVE
                     |
                     v
              page P accessed
                     |
              PTE accessed = 1
                     |
               no page fault
                     |
             later reclaim scan
                     |
                     v
              page_referenced()
                     |
                     v
              "P was referenced"
                     |
                     v
                  ACTIVE
```

That's the answer to your doubt.

---

# 8. Very important: the kernel doesn't immediately react to the accessed bit

This is subtle.

When the CPU sets:

```text
PTE.accessed = 1
```

the kernel doesn't immediately receive an interrupt saying:

```text
"HEY! PAGE P WAS ACCESSED!"
```

There is normally **no kernel entry at that moment**.

Instead:

```text
CPU
 |
 | access
 v
MMU
 |
 | sets accessed bit
 v
PTE
 |
 | no trap
 |
 +------------------------+
                          |
                       sometime later
                          |
                          v
                       VM scan
                          |
                          v
                  inspect accessed bit
```

That's why this mechanism is cheap.

---

# 9. So how does `page_referenced()` work?

In the old 2.6 VM, reclaim code has a function called approximately:

```c
page_referenced(page, ...)
```

Its job is to determine whether the page has recently been referenced through its mappings.

Conceptually:

```text
page_referenced(page)
        |
        v
find mappings of page
        |
        v
inspect PTEs
        |
        v
was PTE accessed/young?
        |
    +---+---+
    |       |
   YES      NO
    |       |
    v       v
referenced  not referenced
```

This is why Linux needs to know the reverse relationship:

```text
struct page
      |
      v
which process/page tables map me?
```

That's the purpose of the **reverse mapping (rmap)** machinery.

---

# 10. Why reverse mapping is needed

Normally you have:

```text
process virtual address
        |
        v
       PTE
        |
        v
      struct page
```

That's forward mapping:

```text
VA -> PTE -> page
```

But reclaim starts with:

```text
struct page
```

and wants to ask:

> Which PTEs currently map this page, and have they been accessed?

So it needs something conceptually like:

```text
struct page
      |
      v
 reverse mapping
      |
      +---- process A PTE
      |
      +---- process B PTE
      |
      +---- process C PTE
```

Then:

```text
page_referenced(page)
        |
        +---- check PTE A
        |
        +---- check PTE B
        |
        +---- check PTE C
```

If any relevant mapping says it was recently accessed, the page is considered referenced.

---

# 11. Your lazy `mmap()` example in detail

Let's do the entire lifecycle.

Suppose:

```c
mmap(..., MAP_PRIVATE | MAP_ANONYMOUS, ...);
```

At this point, Linux can create the **VMA** without necessarily allocating a physical page for every virtual page.

So:

```text
Process virtual address space

0x400000
   |
   v
+----------------+
| VMA            |
| mapped         |
+----------------+

Physical RAM:
(no physical page yet)
```

---

# 12. First access → page fault

Application executes:

```c
*p = 10;
```

CPU tries:

```text
VA 0x400000
```

PTE isn't present.

Therefore:

```text
CPU
 |
 v
PTE = not present
 |
 v
PAGE FAULT
 |
 v
kernel page-fault handler
 |
 v
allocate physical page
 |
 v
PFN 1000
 |
 v
install PTE
 |
 v
PTE -> PFN 1000
 |
 v
resume instruction
```

Now:

```text
VA 0x400000
      |
      v
     PTE
      |
      v
 PFN 1000
      |
      v
 struct page
```

---

# 13. After that: no page fault on every access

Now application does:

```c
*p = 1;
*p = 2;
*p = 3;
...
*p = 100;
```

Assuming the mapping remains present:

```text
CPU
 |
 +---- access
 |
 +---- access
 |
 +---- access
 |
 +---- access
 |
 +---- ...
 |
 v
same PTE
```

There are **no page faults** just because the page is accessed repeatedly.

The MMU handles the accesses.

And the accessed bit remains/set:

```text
PTE
+-------------------------+
| PFN 1000 | accessed=1  |
+-------------------------+
```

---

# 14. What happens to `page->_count` during those 100 accesses?

Basically:

```text
access #1    -> _count unchanged
access #2    -> _count unchanged
access #3    -> _count unchanged
...
access #100  -> _count unchanged
```

That is a critical distinction.

```text
CPU access frequency
        |
        v
 PTE accessed/young bit
```

whereas:

```text
kernel references to page
        |
        v
     page->_count
```

They are completely different concepts.

---

# 15. Example of `_count`

Imagine after allocation:

```text
page->_count = 1
```

Then the kernel temporarily takes another reference:

```text
get_page(page);

page->_count = 2
```

Later:

```text
put_page(page);

page->_count = 1
```

If the final reference is dropped:

```text
put_page(page);

page->_count = 0
```

then the page can become free, subject to the rest of the VM/page-state machinery.

Conceptually:

```text
                 _count

                   1
                   |
              get_page()
                   |
                   v
                   2
                   |
              put_page()
                   |
                   v
                   1
                   |
              put_page()
                   |
                   v
                   0
                   |
                   v
                FREE
```

Again:

```text
_count = ownership/lifetime
```

not:

```text
_count = CPU access count
```

---

# 16. What happens if the page is on the inactive list?

Now let's make it interesting.

Suppose:

```text
ZONE_NORMAL

active_list:
    A -> B -> C

inactive_list:
    D -> E -> P -> F
```

P is your page.

Suppose the application accesses P 1000 times:

```text
CPU
 |
 +-- access P
 +-- access P
 +-- access P
 +-- ...
 +-- access P
```

Hardware:

```text
PTE.accessed = 1
```

Still:

```text
page->_count = same
```

No page fault.

---

# 17. Then `kswapd` starts reclaiming

Suppose free memory is getting low.

```text
free pages
    |
    v
below watermark
    |
    v
kswapd wakes
    |
    v
scan LRU
```

It eventually encounters P:

```text
inactive_list
      |
      v
      P
```

The VM asks:

```text
Has P been referenced recently?
```

It checks the mapping/PTE information.

It discovers:

```text
accessed = 1
```

Therefore:

```text
P is referenced
```

So the page is protected from immediate reclaim.

Depending on the exact 2.6.20 path/page type, it can be kept/rotated or activated; the later 2.6.x implementation makes this distinction explicitly in `page_referenced()`/active-inactive scanning. ([Kernel][1])

---

# 18. Simplified result

For your example:

```text
                  P is INACTIVE
                       |
                       |
              application accesses P
                       |
                       v
                PTE accessed=1
                       |
                 NO PAGE FAULT
                       |
                       |
               memory pressure
                       |
                       v
                  VM scans P
                       |
                       v
              page_referenced(P)
                       |
                       v
                 accessed=1
                       |
                       v
             "P is recently used"
                       |
                       v
              KEEP / ACTIVATE
```

That's the mechanism you were asking about.

---

# 19. But there is an interesting subtlety with the accessed bit

Suppose the kernel checks the accessed bit.

It can't just leave:

```text
accessed = 1
```

forever.

Otherwise the page would appear to have been accessed recently forever.

So conceptually the VM does:

```text
                 check
                   |
                   v
             accessed = 1
                   |
                   v
          "recently referenced"
                   |
                   v
              clear/reset
                   |
                   v
             wait for next
             sampling period
```

Then:

```text
time 1:
    accessed = 1
    kernel checks
    clear

time 2:
    page used?
       |
     +---+---+
    YES      NO
     |        |
     v        v
 accessed=1  accessed=0
```

This gives the kernel a rough "used since last sample" signal.

---

# 20. This is why Linux doesn't need 1000 page faults

Imagine a page gets:

```text
1,000,000 accesses/second
```

If Linux generated a page fault for every access:

```text
1,000,000
       |
       v
page faults/sec
```

That would be catastrophically expensive.

Instead:

```text
1,000,000 CPU accesses
        |
        v
hardware accessed bit = 1
        |
        |
        |     (many accesses)
        |
        v
one VM scan
        |
        v
kernel sees "referenced"
```

This is extremely efficient.

---

# 21. Active → inactive is NOT done every time accessed bit becomes 0

Another important distinction.

The kernel doesn't have:

```text
accessed bit becomes 0
       |
       v
immediately move page to inactive
```

Instead, **LRU scanning itself drives the decision**.

Think:

```text
ACTIVE
  |
  | memory pressure / LRU scanning
  v
candidate selected
  |
  v
check references
  |
  +---- referenced ----> remain/return ACTIVE
  |
  +---- not referenced -> INACTIVE
```

So LRU movement is a **VM policy decision**, not an immediate hardware-bit event.

The 2.6.28-era source, very close to 2.6.20, describes `shrink_active_list()` specifically as moving pages from active to inactive and using `page_referenced()` while doing so. ([Kernel][1])

---

# 22. One subtlety specific to mapped file pages

There is an additional wrinkle in old Linux reclaim.

A newly mapped file page may have an initial reference from the page-table mapping itself. Therefore the reclaim logic sometimes needs more than one observation before deciding that a mapped file page is genuinely hot.

Later 2.6.x `vmscan.c` comments explicitly describe this behavior: mapped file pages get a pass on the inactive list and can be marked `PageReferenced`, with another reference leading to activation. ([Kernel][2])

So don't reduce the algorithm to:

```text
accessed=1 => active
```

for **every page type in every situation**.

The actual 2.6.20 algorithm has page type and mapping details.

---

# 23. `PageReferenced` vs PTE accessed bit

Another source of confusion is that Linux has a software page flag often represented as:

```text
PageReferenced(page)
```

This is **not identical** to the hardware PTE accessed bit.

You can think of two levels:

```text
                 HARDWARE
                    |
                    v
             PTE accessed bit
                    |
                    v
              VM examines it
                    |
                    v
             software page
             reference state
                    |
                    v
          reclaim decision
```

The reclaim code can use page-level reference information as part of deciding whether a page should be activated or reclaimed. Later 2.6.x code, for example, explicitly comments that `page_referenced()` clears `PageReferenced` as part of this process. ([Kernel][3])

---

# 24. Put `_count` into this same example

Let's combine everything.

Initial page:

```text
PFN 1000
struct page
+--------------------------+
| _count = 1               |
| active = 1               |
| referenced = ...         |
| lru -> active_list       |
+--------------------------+
```

CPU accesses it 1000 times:

```text
_count:
    1

PTE accessed:
    1

page reference information:
    eventually observed by VM
```

Not:

```text
_count:
    1001       <-- WRONG
```

The CPU accesses don't increment `_count`.

---

# 25. What actually changes `_count`?

Operations that need to hold a reference to the page can change it.

Conceptually:

```text
                    page
                     |
               _count = 1
                     |
          +----------+----------+
          |                     |
      get_page()             another
          |                  reference
          v
       _count++              _count++
          |
          v
      _count = 2+
```

When those references are released:

```text
put_page()
    |
    v
_count--
```

The reason is:

```text
_kernel currently needs this page_
```

not:

```text
_CPU touched this page_
```

---

# 26. What happens when reclaim isolates the page?

This is another place where `_count` matters.

During reclaim, the kernel needs to safely work with the page while it temporarily removes it from an LRU list.

Conceptually:

```text
inactive_list
      |
      v
 isolate page P
      |
      +---- take temporary reference
      |
      v
 _count increases
      |
      v
 inspect/reclaim page
      |
      v
 release temporary reference
      |
      v
 _count decreases
```

The old 2.6.x `vmscan.c` code even comments that removing active pages from the LRU requires touching `page->_count`. ([Kernel][1])

So `_count` is very relevant to **safe page lifetime management during reclaim**, but still isn't an access counter.

---

# 27. One complete example

Let's use one page `P`.

### Stage 1 — lazy mapping

```text
VMA
 |
 +-- VA 0x400000
       |
       v
    PTE = not present

RAM:
    no physical page yet
```

---

### Stage 2 — first access

```text
CPU access
     |
     v
PAGE FAULT
     |
     v
allocate PFN 1000
     |
     v
PTE -> PFN 1000
     |
     v
page->_count = some reference count
```

---

### Stage 3 — repeated access

```text
CPU:

access P
access P
access P
...
access P 1000 times

          |
          v

PTE.accessed = 1

          |

NO page faults
NO _count increment for each access
```

---

### Stage 4 — memory pressure

```text
free memory low
      |
      v
kswapd
      |
      v
LRU scan
```

Suppose P is active.

The VM eventually examines it:

```text
ACTIVE
  |
  v
reference check
  |
  +---- referenced ---> keep active
  |
  +---- not referenced -> inactive
```

---

### Stage 5 — P becomes inactive

Suppose the page hasn't been accessed for a while:

```text
ACTIVE
  |
  v
not referenced during scan
  |
  v
INACTIVE
```

Now:

```text
inactive_list
    |
    +-- P
```

---

### Stage 6 — application touches it again

```text
CPU
 |
 v
PTE -> PFN 1000
 |
 v
access
 |
 v
PTE.accessed = 1
```

Still:

```text
NO page fault
```

---

### Stage 7 — reclaim sees P

```text
inactive_list
      |
      v
      P
      |
      v
page_referenced(P)
      |
      v
PTE says recently accessed
      |
      v
P is protected from reclaim
      |
      v
KEEP / ACTIVATE
```

So P might move:

```text
INACTIVE
    |
    | referenced again
    v
ACTIVE
```

---

# 28. What if the page is truly unused?

Now imagine:

```text
PTE.accessed = 0
```

and:

```text
Page P = inactive
```

Memory pressure:

```text
inactive P
    |
    v
reclaim
    |
    v
not referenced
    |
    v
Can reclaim?
```

If anonymous:

```text
             anonymous
                 |
                 v
             swap-out
                 |
                 v
          physical frame free
```

If clean file-backed:

```text
          clean file page
                 |
                 v
             discard
                 |
                 v
          physical frame free
```

---

# 29. The entire mechanism in one diagram

```text
                         LAZY mmap()
                             |
                             v
                       VMA created
                             |
                             v
                     first CPU access
                             |
                             v
                       PAGE FAULT
                             |
                             v
                  allocate physical page
                             |
                             v
                    install PTE mapping
                             |
                             v
                         ACTIVE
                             |
                             |
             +---------------+---------------+
             |                               |
       CPU accesses                     CPU doesn't access
             |                               |
             v                               v
      PTE accessed=1                    accessed stays 0
             |                               |
             |                               |
             +---------------+---------------+
                             |
                             v
                     later LRU scanning
                             |
                             v
                    page_referenced()
                             |
                    +--------+--------+
                    |                 |
                 referenced       not referenced
                    |                 |
                    v                 v
              keep/activate       INACTIVE
                                      |
                                      v
                                  reclaim scan
                                      |
                             +--------+--------+
                             |                 |
                         referenced        not referenced
                             |                 |
                             v                 v
                         ACTIVE          reclaimable?
                                               |
                                    +----------+----------+
                                    |                     |
                                   FILE                  ANON
                                    |                     |
                               clean/dirty               SWAP
                                    |                     |
                                    v                     v
                                  FREE                   FREE
```

And independently:

```text
              page->_count
                   |
                   v
       "How many kernel references
        currently keep this page alive?"
                   |
                   |
         NOT CPU access frequency
                   |
                   X
```

---

# 30. The three things to memorize

If you're studying this for kernel work, memorize this table:

| Mechanism                  | Meaning                                       | Changes because of                                |
| -------------------------- | --------------------------------------------- | ------------------------------------------------- |
| **PTE accessed/young bit** | Was this virtual mapping accessed?            | CPU/MMU                                           |
| **Active/Inactive LRU**    | How good is this page as a reclaim candidate? | VM/LRU scanning                                   |
| **`page->_count`**         | How many references keep page alive?          | Kernel `get_page()`/`put_page()`-style operations |

So your original intuition was **correct**:

> "After the first page fault, the CPU can access the page 100 times without causing page faults."

Exactly.

And the answer to:

> "Then how does the kernel know?"

is:

> **It doesn't know immediately. The hardware sets the PTE's accessed bit, and later the VM's reclaim/scanning code examines that information (via page-reference/rmap machinery) when it needs to make an LRU decision.**

That separation — **hardware records access cheaply, kernel samples it later** — is one of the fundamental ideas behind Linux's old LRU reclaim design. ([Kernel][1])

[1]: https://kernel.googlesource.com/pub/scm/linux/kernel/git/sunxi/linux/%2B/refs/tags/v2.6.28/mm/vmscan.c?utm_source=chatgpt.com "mm/vmscan.c - pub/scm/linux/kernel/git/sunxi/linux - Git at Google"
[2]: https://kernel.googlesource.com/pub/scm/linux/kernel/git/stable/linux-stable.git/%2B/refs/tags/v5.4.51/mm/vmscan.c?utm_source=chatgpt.com "mm/vmscan.c - pub/scm/linux/kernel/git/stable/linux-stable.git - Git at Google"
[3]: https://kernel.googlesource.com/pub/scm/linux/kernel/git/hpa/linux-x86-syscalls/%2B/refs/tags/v3.0-rc5/mm/vmscan.c?utm_source=chatgpt.com "mm/vmscan.c - pub/scm/linux/kernel/git/hpa/linux-x86-syscalls - Git at Google"

