
---

# Linux 2.6.20 - Memory Allocators and Caches

All the page allocator, memory management, and cache subsystems are located in the mm/ directory.

---

1. Core Page Allocator (Buddy Allocator)

---

This is the fundamental physical page allocator for the kernel.

Files:

* page_alloc.c : Implements the buddy allocator. Functions like alloc_pages(), __alloc_pages(), free_pages().
* mmzone.c     : Defines struct zone and struct pglist_data for zones and nodes.
* bootmem.c    : Early boot-time physical memory allocator, used before the buddy system is ready.
* highmem.c    : Handles mapping/unmapping of high memory using kmap()/kunmap().

---

2. Virtual Memory and Paging

---

Handles virtual mappings, page faults, and VM areas.

Files:

* memory.c     : Main virtual memory management, page fault handler, mmap/munmap operations.
* mmap.c       : User-space mmap() system call implementation.
* mremap.c, mprotect.c, mlock.c, madvise.c, mincore.c, msync.c : Implement various memory syscalls.
* rmap.c       : Reverse mapping support, tracks which process maps each page.

---

3. Slab and Object Caches

---

Manages kernel object caches (used for internal kernel data structures).

Files:

* slab.c       : Main slab allocator implementation. Functions: kmem_cache_create(), kmem_cache_alloc(), etc.
* slob.c       : Simplified allocator used on small systems (CONFIG_SLOB).
* mempool.c    : Provides preallocated memory pools to avoid allocation failures under memory pressure.

---

4. Virtual Allocation (vmalloc)

---

Allocates virtually contiguous but physically noncontiguous memory for kernel use.

Files:

* vmalloc.c    : Implements vmalloc() and vfree().

---

5. Page Cache (File and I/O Caching)

---

Caches file-backed pages in memory for efficient file I/O.

Files:

* filemap.c    : Core page cache implementation.
* readahead.c  : Implements read-ahead logic.
* truncate.c   : Removes cached pages when files are truncated.
* fadvise.c    : Handles POSIX fadvise() system call for cache behavior hints.

---

6. Reclaim, Swap, and Writeback

---

Handles page reclaim and swapping when memory is low.

Files:

* vmscan.c     : Implements the page reclaim scanner.
* swap.c, swap_state.c, swapfile.c : Handle swapping and page-in/page-out operations.
* page-writeback.c : Controls dirty page writeback to storage.

---

7. NUMA, Migration, and Memory Hotplug

---

Handles memory policies, page migration, and dynamic memory addition.

Files:

* memory_hotplug.c : Adds/removes memory at runtime.
* mempolicy.c      : Implements NUMA memory policies.
* migrate.c        : Handles physical page migration between zones or nodes.

---

8. Supporting and Utility Components

---

Additional files for monitoring, statistics, and helper features.

Files:

* oom_kill.c       : Out-of-memory killer logic.
* vmstat.c         : Virtual memory statistics.
* backing-dev.c    : Writeback and dirty page tracking for backing devices.
* bounce.c         : Bounce buffers for devices that cannot address high memory.
* thrash.c, util.c, internal.h : Miscellaneous support utilities.

---

## Summary Table

Subsystem           | Function Description                         | Key Files
--------------------+----------------------------------------------+------------------------------------
Page Allocator      | Buddy and boot memory allocation             | page_alloc.c, mmzone.c, bootmem.c
Virtual Memory      | VMA management and page faults               | memory.c, mmap.c, rmap.c
Slab Allocator      | Kernel object caching                        | slab.c, slob.c, mempool.c
Virtual Allocator   | Kernel virtual memory (vmalloc)              | vmalloc.c
Page Cache          | File-backed memory cache                     | filemap.c, readahead.c, truncate.c
Reclaim & Swap      | Memory reclaim, swapping, writeback          | vmscan.c, swap*.c, page-writeback.c
NUMA & Hotplug      | NUMA policy and memory migration             | mempolicy.c, migrate.c, memory_hotplug.c
Support Utilities   | OOM handling, stats, helpers                 | oom_kill.c, vmstat.c, backing-dev.c, bounce.c

---

## End of Summary

---

Would you like me to save this exact text as a `.txt` file so you can download it?

