# mlock.c
       mlock(), mlock2(), and mlockall() lock part or all of the calling
       process's virtual address space into RAM, preventing that memory
       from being paged to the swap area.

       munlock() and munlockall() perform the converse operation,
       unlocking part or all of the calling process's virtual address
       space, so that pages in the specified virtual address range can be
       swapped out again if required by the kernel memory manager.

       Memory locking and unlocking are performed in units of whole
       pages.

# mlock
┌──────────────────────────┐
│ User calls mlock(start,len) 
└───────────────┬──────────┘
                │
                ▼
     ┌────────────────────────┐
     │ sys_mlock() syscall    │
     └───────────┬────────────┘
                 │
                 │ 1. Check CAP_IPC_LOCK or rlimit
                 │
                 ▼
     ┌────────────────────────┐
     │ down_write(mmap_sem)   │   ← protects VMA list
     └───────────┬────────────┘
                 │
     (page-align start & len)
                 │
                 ▼
     ┌────────────────────────┐
     │ do_mlock(start,len,ON) │
     └───────────┬────────────┘
                 │
                 │
                 ▼
     ┌───────────────────────────────────────────────┐
     │ vma = find_vma_prev(mm, start, &prev)         │
     └──────────────────────────┬────────────────────┘
                                │
                                ▼
                     (validate VMA exists)
                                │
                                ▼
     ┌───────────────────────────────────────────────┐
     │ Loop over all VMAs intersecting [start,end)   │
     └───────────────┬──────────────────────────────┘
                     │
                     ▼
      For each region inside a VMA:
      ┌───────────────────────────────────────────────┐
      │ newflags = vma->vm_flags | VM_LOCKED (or off) │
      └───────────────────┬───────────────────────────┘
                          │
                          ▼
      ┌───────────────────────────────────────────────┐
      │ mlock_fixup(vma, &prev, nstart, tmp, newflags)│
      └───────────────────┬───────────────────────────┘
                          │
                          ▼
          ┌────────────────────────────────────────────────────┐
          │                mlock_fixup()                       │
          └──────────────────────┬─────────────────────────────┘
                                 │
                                 ▼
    ┌──────────────────────────────────────────────────────────────┐
    │ 1. If flags unchanged: *prev=vma*, return                    │
    └──────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
    ┌──────────────────────────────────────────────────────────────┐
    │ 2. Try merging VMA (vma_merge)                               │
    │    - If merge succeeds: new VMA returned → jump to success   │
    └──────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
    ┌──────────────────────────────────────────────────────────────┐
    │ 3. If merge fails → Split VMA if needed                      │
    │    a) split_vma at start (if start != vma_start)             │
    │    b) split_vma at end   (if end != vma_end)                 │
    │    After splitting: we now have exactly one VMA covering     │
    │    the range [start,end)                                     │
    └──────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
    ┌──────────────────────────────────────────────────────────────┐
    │ 4. SUCCESS path                                               │
    │      vma->vm_flags = newflags                                 │
    └──────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
    ┌──────────────────────────────────────────────────────────────┐
    │ 5. Adjust mm->locked_vm                                       │
    │      pages = (end-start)>>PAGE_SHIFT                          │
    │      If VM_LOCKED set: locked_vm -= pages (pages negative)    │
    └──────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
    ┌──────────────────────────────────────────────────────────────┐
    │ 6. If locking ON and not VM_IO:                               │
    │         make_pages_present(start,end)                         │
    │    → walk page table                                          │
    │    → allocate missing pages                                   │
    │    → fault them in and mark pinned                            │
    └──────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
                  ┌───────────────────────────────┐
                  │ return to do_mlock()          │
                  └──────────────┬────────────────┘
                                 │
            ┌─────────────────────────────────────────┐
            │ do_mlock() moves to next VMA portion    │
            └───────────────────┬─────────────────────┘
                                 │
                                 ▼
               Loop until full range is processed
                                 │
                                 ▼
                 ┌──────────────────────────┐
                 │ up_write(mmap_sem)       │
                 └───────────────┬──────────┘
                                 │
                                 ▼
                    ┌─────────────────────┐
                    │ return to userspace │
                    └─────────────────────┘


# munlock
sys_munlock(start,len)
    ↓
down_write(mmap_sem)
    ↓
do_mlock(start,len,OFF)
    ↓
mlock_fixup() clears VM_LOCKED
    ↓
mm->locked_vm += pages
(no make_pages_present)
    ↓
up_write(mmap_sem)



# mlockall
sys_mlockall(flags)
    ↓
check CAP_IPC_LOCK or rlimit
    ↓
down_write(mmap_sem)
    ↓
set mm->def_flags = VM_LOCKED (if MCL_FUTURE)
    ↓
If MCL_CURRENT:
      for each VMA in mm:
            mlock_fixup(vma, whole range)
    ↓
up_write(mmap_sem)



# How mlock Affects Memory
User VMA:
  ┌──────────────────────────────────────────────┐
  │  Virtual memory area                         │
  │  vm_start → vm_end                           │
  │  vm_flags = read|write|exec|...              │
  │  + VM_LOCKED                                  │  ← added by mlock
  └──────────────────────────────────────────────┘
                    │
                    │ make_pages_present()
                    ▼
          ┌─────────────────────────────┐
          │ Page Table Entries (PTEs)   │
          │  ensure present bit = 1     │
          │  ensure pages allocated     │
          └──────────────┬──────────────┘
                         ▼
             ┌────────────────────────┐
             │ Physical pages          │
             │  • cannot be swapped    │
             │  • can be migrated unless mlock  │
             └────────────────────────┘

