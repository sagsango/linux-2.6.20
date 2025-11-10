# pdflush
This is the subsystem responsible for flushing dirty pages from the page cache → filesystem → block device → disk.
This is one of the most beautiful subsystems in the classic Linux VM, and I will draw it in a way that connects:
- userspace writes
- Linux page cache modifications
- dirty page tracking
- writeback control and thresholds
- pdflush background flusher threads
- filesystem writeback paths
- block I/O (BIO) layer
- disk driver
- physical disk


# High-level File Writeback Pipeline
Userspace write() 
        ↓
File → Page Cache (dirty page)
        ↓
Dirty pages exceed threshold?
        ↓
balance_dirty_pages() 
        ↓
pdflush_operation(writeback_fn, superblock)
        ↓
pdflush thread wakes
        ↓
writeback_fn() → write_cache_pages()
        ↓
Filesystem → map blocks (address_space_ops->writepage)
        ↓
page_io.c → submit_bio(WRITE)
        ↓
Block Layer
        ↓
Disk Driver
        ↓
Disk (actual sectors)


# pdflush_work structure
struct pdflush_work {
    struct task_struct *who;    // this pdflush thread
    void (*fn)(unsigned long);  // callback function (writeback)
    unsigned long arg0;         // argument to fn()
    struct list_head list;      // idle list
    unsigned long when_i_went_to_sleep;
};

# Dirty page created
write() syscall
    ↓
generic_file_write()
    ↓
copy data into page cache
    ↓
mark_page_dirty(page)
    ↓
Dirty pages count ↑


# Dirty Thresholds Trigger Writeback
Dirty pages exceed background threshold?
    ↓
YES → balance_dirty_pages()
    ↓
pdflush_operation(writeback_fn, sb)


# pdflush_operation() — How workers get assigned
pdflush_operation(fn, arg0)
    │
    ├─ lock pdflush_list
    │
    ├─ if no idle worker:
    │        return -1 (caller will retry later)
    │
    ├─ take first idle pdflush_work from pdflush_list
    │
    ├─ pdf->fn = fn
    │
    ├─ pdf->arg0 = arg0
    │
    └─ wake_up_process(pdf->who)

# pdflush thread lifecycle
Start pdflush thread
      │
      ▼
Loop forever:
      │
      ▼
1. Go idle
      add my_work to pdflush_list
      set TASK_INTERRUPTIBLE
      schedule()  ← sleep
      │
      ▼
2. Wake up
      │
      ▼
3. Was a function assigned?
      │
      ├── NO → ignore and continue
      │
      └── YES:
            ↓
4. Run callback:
      my_work->fn(my_work->arg0)
            ↓
5. Decide if more threads needed
            ↓
6. Decide if this thread should exit
            ↓
        Loop back


# The Actual File Writeback Callback Path
pdflush thread wakes
    ↓
fn(sb)   ← filesystem writeback function
    ↓
writeback_inodes(sb)
    ↓
write_cache_pages()
    ↓
address_space_operations->writepage(page)
    ↓
(page_io.c) swap or block write path
    ↓
submit_bio(WRITE)
    ↓
I/O scheduler → block driver → disk



#  FULL SYSTEM DIAGRAM: From Dirty Page to Disk
                 ┌───────────────────────────────────────────────────┐
                 │                  Userspace                        │
                 │                                                   │
                 │        write() syscall                            │
                 └──────────────────────┬────────────────────────────┘
                                        │
                                        ▼
                         ┌────────────────────────────┐
                         │   Linux Page Cache          │
                         │   page becomes dirty        │
                         └────────────────────────────┘
                                        │
                                        ▼
                         Dirty pages exceed threshold?
                                        │
                  ┌─────────────────────┴────────────────────┐
                  │ YES                                       │ NO
                  │                                           │
                  ▼                                           ▼
      balance_dirty_pages()                                   return
                  │
                  ▼
      pdflush_operation(writeback_fn, sb)
                  │
                  ▼
        pick idle pdflush thread
                  │
                  ▼
      wake_up_process(pdf->who)
                  │
                  ▼
 ┌────────────────────────────────────────────────────────────────┐
 │                          pdflush thread                        │
 │                                                                │
 │     1. wake from sleep                                         │
 │     2. run writeback_fn(sb) → filesystem writeback             │
 │     3. clean dirty pages                                       │
 │                                                                │
 └────────────────────────────────────────────────────────────────┘
                  │
                  ▼
        write_cache_pages(mapping)
                  │
                  ▼
  mapping->a_ops->writepage(page, wbc)
                  │
                  ▼
     ┌──────────────────────────────┐
     │ Page Writeback Layer         │
     │ (page_io.c)                  │
     └──────────────────────────────┘
                  │
        get_swap_bio() or FS bio
                  │
       submit_bio(WRITE, bio)
                  │
                  ▼
     I/O Scheduler (elevator)
                  │
                  ▼
     Block Device Driver
                  │
                  ▼
     Physical Disk (sector write)

