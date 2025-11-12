# Shadow page table & extended page table
spt = earlier kvm used these
ept = 

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

# REVERSE MAP (rmap) FLOW
+-------------------------------------------+
| On installing spte → rmap_add(spte)       |
+-------------------------------------------+
            |
            v
+---------------------------------------------+
| page->private:                              |
|   0 → store spte pointer                    |
|   X (even) → convert to descriptor chain    |
|   X|1 (odd) → append to rmap_desc.more      |
+---------------------------------------------+
            |
            v
+----------------------------------------------------------------------+
| On write-protect gfn: rmap_write_protect(gfn)                        |
|   for each spte in rmap chain:                                       |
|       - rmap_remove(spte)                                            |
|       - clear WRITABLE                                               |
|       - flush TLB                                                    |
+----------------------------------------------------------------------+


# GUEST PAGE TABLE WRITE → SHADOW INVALIDATION
+-----------------------------+
| Guest writes to GPA X       |
| (page table page)           |
+-----------------------------+
            |
            v
+------------------------------------+
| kvm_mmu_pre_write(gpa=X, bytes)    |
+------------------------------------+
            |
            v
+-------------------------------------------------+
| find shadow pages whose gfn == X >> PAGE_SHIFT  |
+-------------------------------------------------+
            |
            v
+------------------------------------------------------------+
| if write hits 1 PTE:                                       |
|    - if leaf → rmap_remove(spte)                           |
|    - if non-leaf → unlink child page (mmu_page_remove...)  |
|    - zero spte                                             |
+------------------------------------------------------------+
            |
            v
+------------------------------------------------------------+
| if misaligned OR too many writes:                          |
|    → kvm_mmu_zap_page()                                     |
|        * remove parents                                     |
|        * clear children                                     |
|        * return page to free list                           |
+------------------------------------------------------------+


# NON-PAGING MODE (CR0.PG=0) SHADOW FLOW
+---------------------------+
| paging disabled (PG=0)    |
+---------------------------+
            |
            v
+-----------------------------------------+
| nonpaging_init_context():               |
|   - root_level = 0                      |
|   - shadow_root = PT32E_ROOT_LEVEL (3)  |
|   - allocate shadow roots               |
+-----------------------------------------+
            |
            v
+---------------------------+
| access not mapped         |
+---------------------------+
            |
            v
+--------------------------------------+
| nonpaging_page_fault():              |
|   - GPA = GVA                        |
|   - paddr = gpa_to_hpa()             |
|   - nonpaging_map()                  |
+--------------------------------------+
            |
            v
+--------------------------------------------+
| nonpaging_map():                           |
|   loop level=3→1                            |
|     if entry missing → alloc shadow page    |
|     final level → spte = HPA|P|RW|US        |
+--------------------------------------------+


# Master map
================================================================================
=                             ONE GIANT UNIFIED MAP                            =
================================================================================
 QEMU (userspace)                         KVM (kernel)                          HW
------------------------------------------------------------------------------------
| /dev/kvm open |--> KVM_GET_API_VERSION  ------------------------------------->   |
|               |                                                                CPU|
| KVM_CHECK_EXTENSION ---------------------------------------------------------->   |
|                                                                                    |
| KVM_CREATE_VM ---------> [struct kvm]                                             |
|                          - memslots[]                                             |
|                          - mmu_page_hash[K]                                       |
|                          - active_mmu_pages (list)                                |
|                          - n_free_mmu_pages                                       |
|                                                                                    |
| KVM_CREATE_VCPU -------> [struct kvm_vcpu]                                        |
|                          - regs[], cr0/cr3/cr4/efer                               |
|                          - rip/rflags/cr2                                         |
|                          - svm|vmx (VMCB|VMCS)                                    |
|                          - mmu (ctx, roots, caches)                               |
|                          - run (shared kvm_run)                                   |
|                                                                                    |
| KVM_SET_USER_MEMORY_REGION ---> memslot[i]:                                       |
|     guest_phys_addr..+size  → host mmap base → pages[]                            |
|                                                                                    |
| KVM_SET_CPUID2 / SET_MSRS / SET_REGS / SET_SREGS → seed vcpu state                |
| KVM_CREATE_IRQCHIP / SET_GSI_ROUTING → in-kernel IRQ routing                      |
|                                                                                    |
| KVM_RUN (per vCPU) ------------------------------------------------------------->  |
|  userspace fills kvm_run.req, ioeventfds, mmio buffers                            |
|                                                                                    |
|                               VCPU LOOP (kernel)                                  |
|                 ------------------------------------------------                   |
|                 VCPU_RUN:                                                         |
|                  - load_host_msrs / save_db / fxsave host                          |
|                  - pre_svm_run(): manage ASID, root tables                         |
|                  - enter guest (VMRUN/VMLAUNCH/VMRESUME) ---------------------->  |
|                                                                                   |
|                                                                                   | CPU
| <----------------------------- VMEXIT (exit_code) -------------------------------- |
|                                                                                   |
|  handle_exit(): switch(exit)                                                      |
|   • #PF → kvm_mmu_page_fault()                                                    |
|   • IOIO → fill kvm_run.io, return to QEMU                                        |
|   • MSR  → rdmsr/wrmsr intercept                                                  |
|   • CPUID→ userspace or in-kernel emulate                                         |
|   • HLT  → exit HLT                                                               |
|   • VINTR/INTR → interrupt window mgmt                                            |
|   • SHUTDOWN → re-init VMCB + exit                                                |
|   • CR/DR → emulate + permissions                                                 |
|   • INVLPG → mmu invalidate path                                                  |
|                                                                                   |
|  do_interrupt_requests(): inject pending IRQs via V_IRQ                           |
|  post_kvm_run_save(): write back run->if_flag/cr8/apic_base                        |
|  Re-enter guest or return to QEMU (mmio/io/cpuid/irqwin/signal)                   |
|                                                                                    |
| QEMU handles:                                                                     |
|  - PIO/MMIO (device models: PCI/virtio/APIC/PIC/IDE/serial)                        |
|  - injects IRQ via KVM_IRQ_LINE                                                    |
|  - resumes with KVM_RUN                                                            |
------------------------------------------------------------------------------------


================================================================================
=                      VMEXIT REASONS — SEPARATE DIAGRAMS                       =
================================================================================

[PF: Page Fault]
+-----------------------------+
| VMEXIT: #PF (exit_info_1/2) |
+-----------------------------+
        |
        v
+-----------------------------+
| kvm_mmu_page_fault()        |
|  GVA→GPA walk guest PT      |
|  GPA→HPA via memslot        |
|  build shadow path          |
|   - kvm_mmu_get_page()      |
|   - parent_ptes link        |
|   - leaf spte = HPA|P|RW    |
|  MMIO? set IO-mark, no P    |
+-----------------------------+
        |
        v
[re-enter guest or return if MMIO]

[IOIO: Port I/O]
+----------------------------------+
| VMEXIT: SVM_EXIT_IOIO            |
|  info: port/size/dir/rep/string  |
+----------------------------------+
        |
        v
+------------------------------+
| fill kvm_run->io {           |
|  port/size/dir/string/addr   |
|  count/df                    |
| } → return to userspace      |
+------------------------------+

[MSR: RDMSR/WRMSR]
+-------------------------------+
| VMEXIT: SVM_EXIT_MSR          |
|   exit_info_1: wr ? rd        |
+-------------------------------+
        |
        v
+-------------------------------+
| rd: svm_get_msr(ecx→data)     |
| wr: svm_set_msr(ecx,data)     |
| on #GP → svm_inject_gp()      |
+-------------------------------+
        |
        v
[advance RIP via next_rip]

[CPUID]
+--------------------------+
| VMEXIT: SVM_EXIT_CPUID   |
+--------------------------+
        |
        v
+----------------------------------+
| set exit to KVM_EXIT_CPUID       |
| userspace may emulate/patch      |
+----------------------------------+

[HLT]
+--------------------------+
| VMEXIT: SVM_EXIT_HLT     |
+--------------------------+
        |
        v
+------------------------------+
| if IRQ pending: skip         |
| else: KVM_EXIT_HLT to QEMU   |
+------------------------------+

[VINTR / IRQ Window]
+----------------------------+
| VMEXIT: SVM_EXIT_VINTR     |
+----------------------------+
        |
        v
+--------------------------------------+
| if userspace requested irq window    |
|   set KVM_EXIT_IRQ_WINDOW_OPEN       |
| else allow re-entry or inject IRQ    |
+--------------------------------------+

[INVLPGA/INVLPG]
+------------------------------+
| VMEXIT: SVM_EXIT_INVLPGA     |
+------------------------------+
        |
        v
+------------------------------+
| invalid_op_interception → UD |
+------------------------------+

[SHUTDOWN]
+------------------------------+
| VMEXIT: SVM_EXIT_SHUTDOWN    |
+------------------------------+
        |
        v
+-------------------------------------------+
| VMCB undefined → re-init with init_vmcb() |
| set KVM_EXIT_SHUTDOWN                     |
+-------------------------------------------+

[TASK_SWITCH]
+------------------------------+
| VMEXIT: TASK_SWITCH          |
+------------------------------+
        |
        v
+------------------------------+
| unsupported → EXIT_UNKNOWN   |
+------------------------------+

[EXTERNAL INTERRUPT on exit]
+-----------------------------------+
| exit_int_info = TYPE_INTR         |
+-----------------------------------+
        |
        v
+------------------------------+
| push_irq() into vcpu queue   |
| continue exit handler path   |
+------------------------------+


================================================================================
=                         KVM IOCTLs — SEPARATE DIAGRAMS                        =
================================================================================

[KVM_GET_API_VERSION]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(GET_API_VERSION)    |
+---------+    +----------------------------+
                  returns version (12/…)


[KVM_CHECK_EXTENSION]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(CHECK_EXTENSION, id)|
+---------+    +----------------------------+
                  returns bool/val


[KVM_CREATE_VM]
+---------+    +-----------------------------+
| QEMU    | -> | ioctl(CREATE_VM)            |
+---------+    +-----------------------------+
                  alloc struct kvm
                  init memslots/hash/lists


[KVM_CREATE_VCPU]
+---------+    +------------------------------+
| QEMU    | -> | ioctl(CREATE_VCPU, vcpu_id)  |
+---------+    +------------------------------+
                  alloc struct kvm_vcpu
                  alloc VMCB/VMCS
                  alloc MMU pages/caches
                  map shared kvm_run


[KVM_SET_USER_MEMORY_REGION]
+---------+    +-------------------------------------+
| QEMU    | -> | ioctl(SET_USER_MEM_REGION, slot)    |
+---------+    +-------------------------------------+
                  memslot[slot] = {gpa,size,host,maps}
                  affects GPA→HPA translation


[KVM_SET_CPUID2 / GET_CPUID2]
+---------+    +-------------------------------+
| QEMU    | -> | ioctl(SET/GET_CPUID2)        |
+---------+    +-------------------------------+
                  seeds guest CPUID leaves


[KVM_SET_MSRS / GET_MSRS]
+---------+    +-----------------------------+
| QEMU    | -> | ioctl(SET/GET_MSRS)        |
+---------+    +-----------------------------+
                  batch rd/wr MSRs


[KVM_SET_REGS / GET_REGS]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(SET/GET_REGS)       |
+---------+    +----------------------------+
                  rax,rbx,rcx,... rsp, rip


[KVM_SET_SREGS / GET_SREGS]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(SET/GET_SREGS)      |
+---------+    +----------------------------+
                  segs, crs, gdt,idt, efer


[KVM_CREATE_IRQCHIP]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(CREATE_IRQCHIP)     |
+---------+    +----------------------------+
                  in-kernel PIC/APIC


[KVM_SET_GSI_ROUTING]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(SET_GSI_ROUTING)    |
+---------+    +----------------------------+
                  routes GSI→irqchip/ioeventfd


[KVM_IRQ_LINE]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(IRQ_LINE)           |
+---------+    +----------------------------+
                  assert/deassert line


[KVM_ENABLE_CAP]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(ENABLE_CAP, cap)    |
+---------+    +----------------------------+
                  enable features (e.g., PV)


[KVM_RUN]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(RUN)                |
+---------+    +----------------------------+
                  blocks until VMEXIT
                  returns kvm_run filled


[KVM_SET_TSS_ADDR / SET_IDENTITY_MAP_ADDR]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(SET_*_ADDR)         |
+---------+    +----------------------------+
                  special pages for emulation


[KVM_GET/SET_FPU, GET/SET_LAPIC, GET/SET_XCRS, KVM_SET_NR_MMU_PAGES ...]
+---------+    +----------------------------+
| QEMU    | -> | ioctl(misc state)         |
+---------+    +----------------------------+


================================================================================
=           CPU PIPELINE + VMX/SVM STRUCTURES (STATE & CONTROLS OVERVIEW)       =
================================================================================

[AMD SVM — VMCB]
+----------------------------------------------------------------------------------+
|                               VMCB (per vCPU)                                   |
+----------------------------------------------------------------------------------+
| CONTROL AREA:                                                                    |
|  - intercept masks: CR read/write, DR read/write, EXCEPT(PF), IOIO, MSR, CPUID   |
|  - event_inj (vector/type/valid/err)                                             |
|  - int_ctl (V_IRQ_MASK, V_INTR_PRIO, TPR)                                        |
|  - asid, tlb_ctl (FLUSH_ALL_ASID)                                                |
|  - iopm_base_pa, msrpm_base_pa                                                   |
|  - tsc_offset                                                                     |
+----------------------------------------------------------------------------------+
| SAVE AREA:                                                                        |
|  - segment regs: CS/DS/ES/FS/GS/SS (base/limit/attrib)                            |
|  - GDTR/IDTR, LDTR/TR                                                             |
|  - CR0/CR2/CR3/CR4, EFER                                                          |
|  - RIP/RSP/RFLAGS                                                                 |
|  - DR6/DR7, SYSENTER*, STAR/LSTAR/CSTAR, KERNEL_GS_BASE, SFMASK                   |
+----------------------------------------------------------------------------------+

[Intel VMX — VMCS (high-level, analogous fields)]
+----------------------------------------------------------------------------------+
| VMCS:                                                                            |
|  - Guest-State Area: RIP/RSP/RFLAGS, CR0/CR3/CR4, segment selectors/limits/bases |
|  - Host-State Area: host CR3/CR4, RIP, selectors                                 |
|  - Control Fields: pin/proc/exit/entry controls, EPT/NMI/IO bitmaps, CR masks    |
|  - Exit Info: reason, qualification, IDT-vectoring info, error codes             |
+----------------------------------------------------------------------------------+

[ENTRY/EXIT PIPE]
+--------------------+   vmrun/vmlaunch   +---------------------+
|  Host context      |------------------->|  Guest context      |
|  (fxsave/msrs)     |                    |  (VMCB/VMCS loads)  |
+--------------------+                    +---------------------+
            ^                                 |
            |               vmexit            v
+--------------------+<---------------------+---------------------+
|  Exit handler      |  reason/qual/info    |  Guest caused exit |
|  (handle_exit)     |                      |  (#PF,IO,MSR,...)  |
+--------------------+                      +---------------------+


================================================================================
=                  SHADOW PAGE TABLES — MEMORY LAYOUT & FLOW                    =
================================================================================

[Roles & Indirection]
+-----------------------------------------------------------------------------------+
| kvm_mmu_page (shadow page)                                                        |
|  - gfn                      | role.word (bitfield):                               |
|  - page_hpa                 |   .glevels (guest root lvl)                         |
|  - parent_pte or chain      |   .level   (this shadow level)                      |
|  - root_count               |   .metaphysical (non-guest PT)                      |
|  - slot_bitmap              |   .quadrant (for 32-bit shadowing)                  |
|  - global/multimapped       |                                                     |
+-----------------------------------------------------------------------------------+

[Levels — 64-bit guest]
+--------------------+     +--------------------+     +--------------------+     +--------------------+
| PML4 (L4)          | --> | PDPT (L3)          | --> | PD (L2)            | --> | PT (L1)            |
| shadow L4          |     | shadow L3          |     | shadow L2          |     | shadow L1 (leaf)   |
+--------------------+     +--------------------+     +--------------------+     +--------------------+
       |                         |                         |                          |
       | parent_pte links        | parent_pte links        | parent_pte links         | spte = HPA|P|RW|...
       v                         v                         v                          v

[Allocation / Link]
+----------------------------------------------+
| kvm_mmu_get_page(gfn, gaddr, level, role)    |
|  - lookup hash(bucket)                       |
|  - if miss: take from vcpu->free_pages       |
|  - set page->role, page->gfn                 |
|  - mmu_page_add_parent_pte(page, parent_pte) |
+----------------------------------------------+

[Install leaf SPTE]
+-----------------------------------------------+
| set_pte_common():                             |
|  - access bits (P/U/W/NX + shadow marks)      |
|  - HPA via gpa_to_hpa()                       |
|  - rmap_add(spte)                             |
|  - mark_page_dirty if writable                |
|  - mark pagetable non-global if needed        |
+-----------------------------------------------+

[Reverse Map (rmap)]
+-------------------------------+      +-------------------------------+
| page->private = 0/ptr/ptr|1   | ---> | kvm_rmap_desc chain           |
+-------------------------------+      +-------------------------------+
         | single spte                          | multiple sptes
         v                                      v
   rmap_remove / write_protect          rmap_desc_remove_entry()

[Guest PT Write → Invalidation]
+----------------------------------------------+
| kvm_mmu_pre_write(gpa, bytes):               |
|  - find shadow page(s) for gfn               |
|  - if single PTE hit: remove child or rmap   |
|  - zero spte                                 |
|  - if misaligned or flooded: zap page        |
|      kvm_mmu_zap_page():                     |
|        • unlink children                     |
|        • clear parent ptes                   |
|        • free to free_pages or active list   |
+----------------------------------------------+

[Root Management]
+----------------------------------------------+
| mmu_alloc_roots():                           |
|  - 64-bit: shadow root = PT64_ROOT_LEVEL     |
|  - 32E: PAE quad roots in DMA32 page         |
|  - 32: shadow_root = PT32E_ROOT_LEVEL        |
|  set CR3 to shadow root (with PCD/WPT)       |
+----------------------------------------------+

[Non-Paging (CR0.PG=0)]
+----------------------------------------------+
| root_level=0; shadow_root=PT32E_ROOT_LEVEL   |
| nonpaging_map(): builds 3-level shadow       |
| leaf spte = GPA→HPA | P|RW|US                |
+----------------------------------------------+

# MMU
   ┌──────────────────────────────┐
   │ GVA = Guest Virtual Address  │  (CPU inside guest issues loads/stores)
   └──────────────────────────────┘

   ┌──────────────────────────────┐
   │ GPA = Guest Physical Address │  (guest thinks this is "RAM")
   └──────────────────────────────┘

   ┌──────────────────────────────┐
   │ HPA = Host Physical Address  │  (real host RAM PFN under Linux)
   └──────────────────────────────┘

   ┌─────────────────────────────────────────┐
   │ QEMU VA = userspace virtual address     │
   │   (where QEMU's RAM buffer is mmap’d)   │
   └─────────────────────────────────────────┘



guest page tables:
    CR3 → PML4 → PDPT → PD → PT → PTE → GPA

    GVA → GPA  (guest page tables)
    GPA → HPA  (memslot from QEMU)




GVA 
  → guest page tables
      → GPA
         → memslots (from QEMU)
             → HPA





                  CPU EXECUTES IN GUEST
                  GVA = 0x7fffdeadbeef
                          |
                          v
     (1) Guest paging     |
         walk             |
                          v
     +---------------------------------------+
     | Guest Page Tables (in guest memory)   |
     | CR3 → PML4 → PDPT → PD → PT → PTE     |
     +---------------------------------------+
                          |
                          v
                  GPA = 0x0012A000
                          |
                          v
     (2) KVM converts GPA → HPA via memslots
                          |
                          v
     +----------------------------------------+
     | KVM memslots                           |
     | gpa range → host pages → HPA           |
     +----------------------------------------+
                          |
                          v
                    HPA = pf#12345 << 12
                          |
                          v
     (3) KVM writes SHADOW PTE pointing to HPA
                          |
                          v
     +----------------------------------------+
     | Shadow page tables (created by KVM)    |
     | GVA → HPA                              |
     +----------------------------------------+
                          |
                          v
                 HOST RAM ACCESS HAPPENS


# Master MMU
                                            ┌──────────────────────────────────────────┐
                                            │               HOST RAM (HPA)             │
                                            │  (actual DRAM; PFNs allocated by Linux) │
                                            └──────────────────────────────────────────┘
                                                              ▲
                                                              │(physical PFN)
                                                              │
                     +----------------------------------------┼--------------------------------------+
                     |                                        │                                      |
                     |                                        │                                      |
                     v                                        v                                      v

    ┌─────────────────────────────┐        ┌────────────────────────┐         ┌──────────────────────────┐
    │ QEMU Virtual Address Space │        │    KVM memslots[]       │         │  KVM Shadow Page Tables  │
    └─────────────────────────────┘        └────────────────────────┘         └──────────────────────────┘
                 │                                  │                                      │
                 │ mmap() guest RAM                 │ GPA→HPA mapping                     │ GVA→HPA mapping
                 v                                  v                                      v

  QEMU VA (Host userspace)                          GPA Space                          Shadow PT (host pages)
  e.g. 0x7fabc0000000                                e.g. 0x000...fff0                  allocated by KVM
            │                                               │                                     │
            │ (host userspace PTEs: QEMU VA→HPA)            │                                     │
            │                                               │                                     │
            v                                               v                                     v
    ┌────────────────────────────────────┐         ┌────────────────────────────────────────┐
    │ QEMU page tables (Linux kernel PT) │         │ memslot:                                │
    │   VA→PA                             │         │   .guest_phys_addr range                │
    │   VA→PFN                            │         │   .userspace_addr (QEMU VA region)      │
    └────────────────────────────────────┘         │   → resolved to host struct page → PFN   │
                 │                                 └────────────────────────────────────────┘
                 │                                               │
                 v                                               v
           HPA = <actual machine PFN>  ◀─────────────────────────┘
                      (REAL RAM)




                ┌────────────────────────────┐
                │       Guest CPU executes   │
                │       instruction using    │
                │       Guest Virtual Addr   │
                └────────────────────────────┘
                               │
                               ▼
                         (1) GVA
                               │
                               ▼
          ┌──────────────────────────────────────────┐
          │ Guest Page Tables                        │
          │ (guest CR3; guest memory)                │
          └──────────────────────────────────────────┘
                               │
                               ▼
                         (2) GPA
                               │
                               ▼
          ┌─────────────────────────────────────────────┐
          │ KVM memslots                                 │
          │ GPA → QEMU VA → host struct page → PFN → HPA │
          └─────────────────────────────────────────────┘
                               │
                               ▼
                         (3) HPA
                               │
                               ▼
          ┌──────────────────────────────────────────────┐
          │ KVM SHADOW PAGE TABLES                        │
          │ (GVA → HPA installed into real hardware PT)   │
          └──────────────────────────────────────────────┘
                               │
                               ▼
                     (4) hardware DRAM access





   ┌──────────────┐       ┌──────────────┐        ┌──────────────┐       ┌──────────────┐
   │    GVA       │  -->  │ Guest PTs    │  -->   │    GPA       │  -->  │   memslots   │
   └──────────────┘       └──────────────┘        └──────────────┘       └──────────────┘
                                                                               │
                                                                               ▼
                         ┌──────────────┐  <--Linux PT--  ┌──────────────┐
                         │  QEMU VA     │ ---------------->│     HPA      │
                         └──────────────┘                  └──────────────┘
                                      ▲                           │
                                      │                           ▼
                                      └──── shadow PT stores HPA ─┘




                 ======================= BIG KVM MEMORY MAP =======================


GUEST           GUEST PT           KVM MEMSLOTS        QEMU PROCESS        HOST RAM
-----------------------------------------------------------------------------------------------

GVA ───────▶ [guest PML4] ───▶ GPA ───────▶ memslot ─────▶ QEMU VA ─────▶ HPA ───▶ DRAM cell
                 ▲                 │                       │               │
                 │                 │ GPA→QEMUVA            │ Linux PTs     │ PFN #123456
                 │                 │                       │               │
                 │                 └──────▶ struct page ───┘               ▼
                 │                              │                       physical DIMMs
                 │                              ▼
Shadow PT ◀──────┴────────────── installs HPA into SPTE
(GVA→HPA mapping)


===============================================================================================






