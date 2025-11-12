# FULL KVM ARCHITECTURE (QEMU ↔ KVM ↔ CPU ↔ MMU ↔ DEVICES) 
+--------------------------------------------------------------------------------------+
|                                    QEMU USERSpace                                   |
|--------------------------------------------------------------------------------------|
|  +----------------+       +----------------------+       +------------------------+   |
|  | /dev/kvm open  |       | KVM_CREATE_VM        |       | KVM_CREATE_VCPU        |   |
|  +----------------+       +----------------------+       +------------------------+   |
|          |                          |                               |               |
|          v                          v                               v               |
|  +----------------+       +----------------------+       +------------------------+   |
|  | Memory mmap()  |-----> | SET_USER_MEM_REGION |-----> | KVM_RUN (per vcpu)     |   |
|  +----------------+       +----------------------+       +------------------------+   |
|                               |                                           |           |
|                               | (RAM backing: mmap’d host pages)          |           |
|                               v                                           |           |
|  +-------------------------------------------------------------------------------+    |
|  |  Userspace Device Emulation (MMIO/PIO/IRQ/APIC/PIC/virtio/IDE/PCI/etc.)      |    |
|  +-------------------------------------------------------------------------------+    |
+--------------------------------------------------------------------------------------+
                                       |
                                       v
+--------------------------------------------------------------------------------------+
|                                   KVM KERNEL MODULE                                   |
|----------------------------------------------------------------------------------------|
|                                                                                         |
|  +----------------------------+     +-----------------------------------------------+   |
|  | struct kvm (VM struct)    |     | struct kvm_vcpu                               |   |
|  | - memslots[]              |     | - run struct                                   |   |
|  | - assigned host pages     |     | - regs                                         |   |
|  | - active_mmu_pages list   |     | - cr0/cr3/cr4/efer                             |   |
|  | - n_free_mmu_pages        |     | - mmu context                                  |   |
|  +----------------------------+     +-----------------------------------------------+   |
|                     |                                 |                               |
|                     v                                 v                               |
|  +--------------------------------------+   +-----------------------------------------+|
|  | KVM ioctls handler (/dev/kvm)        |   |   VCPU Execution Loop (run_vcpu())      ||
|  | - KVM_SET_USER_MEMORY_REGION         |   |   - enter guest (VMRUN/VMLAUNCH)        ||
|  | - KVM_GET_REGS / SET_REGS            |   |   - handle VMEXIT                       ||
|  | - KVM_RUN                            |   |   - reenter guest                       ||
|  +--------------------------------------+   +-----------------------------------------+|
|                                                         |
|                                                         v
|   +----------------------------------------------------------------------------------+ |
|   |                         VMEXIT MAIN DISPATCHER (handle_exit)                     | |
|   |----------------------------------------------------------------------------------| |
|   |  Exit Reason (VMX/SVM):                                                          | |
|   |    - #PF  (page fault) → kvm_mmu_page_fault()                                   | |
|   |    - MMIO read/write → prepare kvm_run->mmio                                     | |
|   |    - PIO read/write  → kvm_run->io                                               | |
|   |    - CPUID instruction → emulate                                                 | |
|   |    - HLT → exit to userspace                                                     | |
|   |    - APIC/PIC events → route to userspace or kernel apic emulator                | |
|   |    - CR access → handle_cr()                                                     | |
|   |    - MSR access → handle_msr()                                                   | |
|   |    - TSC offset update                                                           | |
|   |----------------------------------------------------------------------------------| |
|                                                                                         |
+-----------------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------------+
|                               MMU (SHADOW PAGING ENGINE)                                 |
|------------------------------------------------------------------------------------------|
|  +----------------------------+    +---------------------------------------------------+  |
|  | Shadow Page Tables         |    |  Reverse Mapping (rmap)                           |  |
|  | - allocated via alloc_page |    |  page->private =                                   |  |
|  | - per-VCpu free_pages list |    |     0 → no spte                                   |  |
|  | - tracked via kvm_mmu_page |    |     ptr → 1 spte                                  |  |
|  +----------------------------+    |     ptr|1 → chain of kvm_rmap_desc                |  |
|                                   +---------------------------------------------------+  |
|                                   | Operations:                                         |
|                                   |  - rmap_add                                        |
|                                   |  - rmap_remove                                     |
|                                   |  - rmap_write_protect                              |
|                                   +----------------------------------------------------+ |
|                                                                                          |
|  +--------------------------------------------------------------------------------------+ |
|  | kvm_mmu_page_fault():                                                                | |
|  |   - walk GVA→GPA from guest PTs                                                      | |
|  |   - GPA→HPA via memslots                                                             | |
|  |   - build/repair shadow PT levels                                                    | |
|  |   - install spte (set_pte_common)                                                    | |
|  |   - for MMIO: install NON-PRESENT + IO MARK spte                                     | |
|  +--------------------------------------------------------------------------------------+ |
+-------------------------------------------------------------------------------------------+


# CPU ENTER / EXIT FULL ASCII FUNCTIONAL FLOW
+---------------------------+
|  VCPU run loop            |
+---------------------------+
            |
            v
+---------------------------+
|  load guest regs          |
|  restore CR2/DR6/DR7      |
|  restore MSRs             |
|  VMRUN/VMLAUNCH           |
+---------------------------+
            |
            v (hardware exit)
+---------------------------+
|         VMEXIT            |
|   exit_reason = ?         |
+---------------------------+
            |
            v
+---------------------------------------+
|           handle_exit()               |
+---------------------------------------+
| switch(exit_reason):                 |
|  - PF     → kvm_mmu_page_fault()     |
|  - IO     → io_exit handler          |
|  - MMIO   → mmio_exit handler        |
|  - CPUID  → cpuid_emulate            |
|  - HLT    → return to userspace      |
|  - MSR    → msr_intercept            |
|  - CR     → cr_intercept             |
|  - NPF(SVM) → nested page fault      |
+---------------------------------------+
            |
            v
+---------------------------+
|    re-enter guest?        |
+---------------------------+
     | yes          | no
     v              v
+-----------+    +-------------+
| VMENTRY   |    | return to   |
| (resume)  |    | userspace   |
+-----------+    +-------------+


# MEMORY FLOW (QEMU → memslots → KVM → GPA→HPA → shadow)
+------------------------------+
| QEMU allocates RAM (mmap)    |
+------------------------------+
                |
                v
+-----------------------------------------+
| KVM_SET_USER_MEMORY_REGION ioctl        |
+-----------------------------------------+
| Creates memslot:                        |
|   .guest_phys_addr range                |
|   .userspace_addr (host mmap)           |
|   .npages                               |
|   .pages[] (struct page*) optional      |
+-----------------------------------------+
                |
                v
+-----------------------------------------+
| gpa_to_hpa():                           |
+-----------------------------------------+
| slot = gfn_to_memslot(gfn)              |
| page = gfn_to_page(slot, gfn)           |
| hpa  = page_to_pfn(page) << 12          |
+-----------------------------------------+
                |
                v
+-----------------------------------------+
| Shadow PTE stores:                      |
|   - HPA                                 |
|   - PRESENT                             |
|   - WRITABLE (if allowed)               |
|   - USER/SUPERVISOR                     |
|   - NX                                  |
+-----------------------------------------+



# MMIO FLOW (guest → shadow IO spte → exit → QEMU device model)
+---------------------------+
| Guest executes access     |
| (load/store to MMIO GPA)  |
+---------------------------+
            |
            v
+-----------------------------------------------------------+
| shadow PTE for GPA: NON-PRESENT + PT_SHADOW_IO_MARK       |
+-----------------------------------------------------------+
            |
            v
+-----------------------------+
| Hardware generates #PF      |
+-----------------------------+
            |
            v
+-------------------------------+
| VMEXIT (reason: EPT/NPT off, |
|         non-present PTE+IO)  |
+-------------------------------+
            |
            v
+---------------------------------------------+
| handle_mmio(): fill kvm_run->mmio struct    |
+---------------------------------------------+
            |
            v
+---------------------------+
| return to QEMU (KVM_RUN)  |
+---------------------------+
            |
            v
+--------------------------------+
| QEMU device model handles I/O  |
| (virtio, PCI, APIC, etc.)      |
+--------------------------------+
            |
            v
+------------------------------+
| KVM_RUN again resumes guest  |
+------------------------------+


# SHADOW MMU PAGE FAULT FLOW
+----------------------------------+
| VMEXIT (#PF, exit qualification) |
+----------------------------------+
            |
            v
+-----------------------------------------+
| kvm_mmu_page_fault(vcpu, gva, errcode)  |
+-----------------------------------------+
            |
            v
+--------------------------------------------+
| walk guest page tables (GVA → GPA):        |
|   - read guest CR3                         |
|   - read guest PDEs/PTEs                   |
|   - check presence, R/W, U/S bits          |
+--------------------------------------------+
            |
            v
+-----------------------------------------+
| GPA → HPA via memslots                  |
| if slot missing → spte = IO type        |
+-----------------------------------------+
            |
            v
+------------------------------------------------------+
| build shadow path (root → ... → leaf)                |
|   for each level:                                    |
|     - kvm_mmu_get_page(gfn, role)                    |
|         • lookup shadow page                         |
|         • allocate if missing                        |
|         • link parent_pte → child page               |
|     - fill parent spte with child HPA                |
+------------------------------------------------------+
            |
            v
+-----------------------------------------------+
| leaf spte creation: set_pte_common():         |
|   - put HPA                                   |
|   - add PRESENT                               |
|   - add WRITABLE if allowed                   |
|   - mark_page_dirty if writable               |
|   - rmap_add()                                |
+-----------------------------------------------+
            |
            v
+----------------------------+
| re-enter guest (VM entry)  |
+----------------------------+

