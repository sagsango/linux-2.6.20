# systems call
+--------------+------------------------------------------------------------+--------------------------------------------------------------+
| System Call  | Prototype                                                  | Purpose                                                      |
+--------------+------------------------------------------------------------+--------------------------------------------------------------+
| shmget()     | int shmget(key_t key, size_t size, int shmflg);            | Create or obtain a shared-memory segment and return its ID   |
| shmat()      | void *shmat(int shmid, const void *shmaddr, int shmflg);   | Attach an existing shared segment into the process address   |
|              |                                                            | space                                                        |
| shmdt()      | int shmdt(const void *shmaddr);                            | Detach (unmap) a previously attached segment                 |
| shmctl()     | int shmctl(int shmid, int cmd, struct shmid_ds *buf);      | Control/query the segment (permissions, remove, info, etc.)  |
+--------------+------------------------------------------------------------+--------------------------------------------------------------+
| NOTE: shmget(), shmat(), shmdt(), shmctl() are user-visible syscalls; do_shmat() is the internal kernel helper.                          |
+------------------------------------------------------------------------------------------------------------------------------------------+


# general flow
sys_shmget()
   └─ newseg()
        ├─ allocate struct shmid_kernel
        ├─ create tmpfs file (shmem)
        └─ register in ipc_ids[]

sys_shmat()
   └─ do_shmat()
        ├─ create vm_area_struct for mapping
        ├─ link to shmem file (tmpfs inode)
        └─ ++shm_nattch

page fault
  → do_page_fault()
      → vma->vm_ops->nopage() (shmem_nopage)
      → allocate page in shmem inode
      → map into your PTE



# More on systems calls
sys_shmget()
   ├─ look up key in ipc_ids[IPC_SHM_IDS]
   ├─ if not found & IPC_CREAT set → newseg()
   │     ├─ alloc struct shmid_kernel
   │     ├─ create tmpfs file (shmem_file_setup)
   │     ├─ fill perms, size, owner
   │     └─ insert into IPC table (ipc_addid)
   └─ return shmid (index + seq)


do_shmat()
   ├─ shm_lock(shmid) → get shmid_kernel
   ├─ check permissions (ipcperms)
   ├─ decide virtual address (align to SHMLBA)
   ├─ call do_mmap(file, addr, size, prot, flags)
   │     └─ creates vm_area_struct
   │          ↳ vma->vm_file = shm_file (tmpfs)
   │          ↳ vma->vm_ops  = shm_vm_ops
   ├─ ++shm_nattch
   └─ return attached address to user


sys_shmdt()
   ├─ find_vma(mm, shmaddr)
   ├─ do_munmap(mm, vma->vm_start, vma->vm_end - vma->vm_start)
   │     → removes VMA
   ├─ shm_close(vma)
   │     ├─ --shm_nattch
   │     └─ if (nattch==0 && SHM_DEST) → shm_destroy()
   └─ return 0


sys_shmctl()
   ├─ lock shm_ids
   ├─ switch(cmd)
   │     ├─ IPC_INFO → return global limits
   │     ├─ IPC_STAT → copy shmid64_ds to user
   │     ├─ IPC_SET  → update perms, mode
   │     ├─ IPC_RMID → do_shm_rmid()
   │     ├─ SHM_LOCK → shmem_lock()
   │     └─ ...
   └─ unlock


# shm linkage:
──────────────────────────────────────────────────────────────────────────────
              FULL STRUCTURE LINKAGE FLOW (Linux 2.6.20)
──────────────────────────────────────────────────────────────────────────────
Process A                        Process B
──────────                       ──────────
mm_struct                        mm_struct
   │                                │
   ├─ vm_area_struct (→ same file)  ├─ vm_area_struct (→ same file)
   │        │                       │        │
   │        └── vm_file ────────────┘        │
   │                 │                       │
   │                 ▼                       ▼
   │             shmid_kernel ───────►  (shared descriptor)
   │                 │
   │                 ▼
   │             shmem file (tmpfs inode)
   │                 │
   │                 ▼
   │             address_space → list of struct page (actual data)
   │
   ▼
ipc_ids[IPC_SHM_IDS]  (index)
   │
   ▼
ipc_namespace (usually init_ipc_ns)
──────────────────────────────────────────────────────────────────────────────



═══════════════════════════════════════════════════════════════════════════════
              LINUX 2.6.20 — SYSTEM V SHARED MEMORY INTERNAL LAYOUT
═══════════════════════════════════════════════════════════════════════════════

                ┌────────────────────────────────────────────┐
                │           [ipc_namespace]                  │
                │ (normally init_ipc_ns — global namespace)  │
                └────────────────────────────────────────────┘
                                │
                                ▼
                ┌────────────────────────────────────────────┐
                │           [ipc_ids: IPC_SHM_IDS]           │
                │  Table of all shmid_kernel entries         │
                │  (indexed by shmid)                        │
                └────────────────────────────────────────────┘
                                │
                                ▼
                ┌────────────────────────────────────────────┐
                │           [shmid_kernel]                   │
                │  (One per shared-memory segment)           │
                │--------------------------------------------│
                |shmid_kernel.shm_perm:                      |
                │ key        = 0x1234                        │
                │ id         = 0x00020002                    │
                │ shm_segsz  = 4096 bytes                    │
                │ shm_nattch = 2  (two processes attached)   │
                │ shm_file ───────────────────────────────────┐
                │ permissions, uid/gid, times, etc.          |│
                └────────────────────────────────────────────┘│
                                                              │
                                                              ▼
                ┌────────────────────────────────────────────┐
                │            [struct file *shm_file]         │
                │  (created by shmem_file_setup())           │
                │--------------------------------------------│
                │ f_op  = &shm_file_operations               │
                │ f_path.dentry->d_inode ─────────────────────┐
                │ private_data → ipc_namespace pointer       ││
                └──────────────────────────────────────────--┘│
                                                              │
                                                              ▼
                ┌────────────────────────────────────────────┐
                │             [inode (tmpfs)]                │
                │--------------------------------------------│
                │ i_mapping → address_space                  │
                │ i_size = shm_segsz                         │
                │ (represents the actual pages in memory)    │
                └────────────────────────────────────────────┘
                                │
                                ▼
                ┌────────────────────────────────────────────┐
                │          [address_space / page cache]      │
                │--------------------------------------------│
                │ struct page *page1 → physical frame X      │
                │ struct page *page2 → physical frame Y      │
                │ ...                                        │
                └────────────────────────────────────────────┘
                                ▲
             ┌──────────────────┼──-────────────────┐
             │                                      │
             │                                      │
─────────────┴───────────────┐       ┌──────────────┴───────────────
Process A (PID 1001)         │       │        Process B (PID 1002)
─────────────────────────────┘       └─────────────────────────────
mm_struct (A)                        mm_struct (B)
│                                   │
├─ vm_area_struct #1 (text)         ├─ vm_area_struct #1 (text)
├─ vm_area_struct #2 (heap)         ├─ vm_area_struct #2 (heap)
│                                   │
├─ vm_area_struct #3 (shm segment)  ├─ vm_area_struct #3 (shm segment)
│   ├─ vm_start = 0x7fabc000        │   ├─ vm_start = 0x7facd000
│   ├─ vm_end   = 0x7fabcfff        │   ├─ vm_end   = 0x7facdfff
│   ├─ vm_file  → shm_file ─────────┘   ├─ vm_file  → same shm_file
│   ├─ vm_ops   = &shm_vm_ops           ├─ vm_ops   = &shm_vm_ops
│   └─ vm_flags = VM_SHARED|READ|WRITE  └─ vm_flags = VM_SHARED|READ|WRITE
│
└─ linked into mm->mmap list             └─ linked into mm->mmap list

───────────────────────────────────────────────────────────────────────────────
              Both VMAs → same file → same inode → same address_space
                       ⇒ both share identical physical pages
───────────────────────────────────────────────────────────────────────────────





