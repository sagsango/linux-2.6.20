# KVM Architecture — Big Picture
              ┌────────────────────────────┐
              │        QEMU Process        │
              │  uses /dev/kvm ioctls      │
              └────────────┬───────────────┘
                           │
                           ▼
                ┌──────────────────────┐
                │   KVM Core Driver    │
                │ (this file: kvm.c)   │
                └────────┬─────────────┘
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
  kvm_mmu.c        kvm_x86_ops        /dev/kvm
 (shadow PT)       (arch hooks)       miscdevice
   │
   ▼
hardware MMU ↔ CPU VMX/SVM extensions ↔ guest




# /dev/kvm Lifecycle Overview
QEMU opens /dev/kvm
        │
        ▼
 ┌──────────────────────────┐
 │ kvm_dev_open()           │
 │  alloc struct kvm        │
 │  init vcpu[] array       │
 │  init memslots[]         │
 └──────────────────────────┘
        │
        ▼
 ┌──────────────────────────────┐
 │ ioctls through kvm_dev_ioctl │
 └──────────────────────────────┘
        │
        ├── KVM_CREATE_VCPU → create vcpu
        ├── KVM_SET_MEMORY_REGION → alloc guest RAM
        ├── KVM_RUN → execute vcpu
        └── other GET/SET ioctls


# Major Data Structures
struct kvm
 ├─ struct kvm_vcpu vcpus[KVM_MAX_VCPUS]
 │   ├─ struct kvm_mmu mmu
 │   │   ├─ shadow page tables
 │   │   ├─ gva_to_gpa function
 │   │   └─ caches, free_pages
 │   ├─ control regs: cr0,cr3,cr4,efer
 │   ├─ regs[16], rip, rflags
 │   ├─ mmio state
 │   └─ spinlocks, mutex
 │
 ├─ struct kvm_memory_slot memslots[KVM_MEMORY_SLOTS]
 │   ├─ base_gfn
 │   ├─ npages
 │   ├─ struct page** phys_mem
 │   ├─ dirty_bitmap
 │   └─ flags
 │
 ├─ spinlock_t lock
 ├─ int nmemslots
 └─ misc flags: busy, memory_config_version



# Memory Region Allocation (KVM_SET_MEMORY_REGION)
QEMU: ioctl(KVM_SET_USER_MEMORY_REGION)
        │
        ▼
┌────────────────────────────────────┐
│ kvm_dev_ioctl_set_memory_region()  │
│   - sanity checks                  │
│   - vmalloc(page** phys_mem)       │
│   - alloc_page() per guest page    │
│   - build memslot:                 │
│       base_gfn → struct page[]     │
│   - install slot into kvm->memslots│
│   - reset vcpu MMU contexts        │
└────────────────────────────────────┘
        │
        ▼
Guest GPA range now points to host pages.



Guest GPA (0x0000..) ─┬──────────┐
                      ▼          │
          memslot[i]: base_gfn → struct page*[npages]
                      │          │
                      ▼          ▼
                  alloc_page() → host PFN → real RAM



# VCPU Creation (KVM_CREATE_VCPU)
QEMU ioctl(KVM_CREATE_VCPU, n)
        │
        ▼
┌────────────────────────────────────┐
│ kvm_dev_ioctl_create_vcpu()       │
│   → vcpu = &kvm->vcpus[n]         │
│   → alloc fxsave buffers          │
│   → kvm_arch_ops->vcpu_create()   │
│   → kvm_mmu_create()              │
│   → kvm_mmu_setup()               │
│   → vcpu_setup() (arch init)      │
└────────────────────────────────────┘


# Execution Flow (KVM_RUN)
QEMU
 ├── fills struct kvm_run
 ├── ioctl(KVM_RUN)
 ▼
 ┌──────────────────────────────────────┐
 │ kvm_dev_ioctl_run()                 │
 │   vcpu = vcpu_load()                │
 │   -> lock, arch->vcpu_load()        │
 │   arch->run(vcpu, run)              │
 │     │                               │
 │     ├─ vm-entry (VMX/SVM)           │
 │     │   → hardware guest mode       │
 │     ├─ VMEXIT (exit_code)           │
 │     │   → handle_exit()             │
 │     │       → IO/MMIO/HLT/#PF...    │
 │     │       → fill kvm_run struct   │
 │     └─ return to userspace          │
 │   vcpu_put()                        │
 └──────────────────────────────────────┘

# Memory Translation Path (used by emulator, read/write_guest)
GVA → gva_to_hpa(vcpu, addr)
         │
         ▼
   guest page tables → GPA
         │
         ▼
   gfn_to_memslot() → struct page*
         │
         ▼
   page_to_pfn() → HPA
         │
         ▼
   kmap_atomic(pfn_to_page) → temporary HVA
         │
         ▼
   memcpy() → host buffer


GVA ──▶ GPA ──▶ HPA ──▶ host kernel mapping (kmap_atomic)


# Control Register Updates (set_cr0, set_cr3, set_cr4)
set_cr0()
 ├─ check reserved bits
 ├─ validate PE/PG flags
 ├─ if enabling paging → load_pdptrs()
 ├─ arch_ops->set_cr0()
 └─ kvm_mmu_reset_context(vcpu)

set_cr3()
 ├─ validate reserved bits
 ├─ check memslot for CR3 gfn
 ├─ vcpu->mmu.new_cr3(vcpu)
 └─ resets shadow root

set_cr4()
 ├─ reserved bits
 ├─ long mode checks (PAE)
 ├─ set_cr4(), reset context


      CRx write from guest
             │
             ▼
     validate bits
             │
             ▼
   update vcpu->crx shadow
             │
             ▼
   reset MMU (flush shadow roots)



# gfn_to_memslot() — GPA → Host Page lookup

struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn)
{
  for (each memslot)
     if (gfn in [base_gfn, base_gfn+npages))
         return memslot;
}


So any GPA → memslot (→ struct page* array) lookup path:
GPA >> 12 = gfn
for all memslots:
   if base ≤ gfn < base+npages:
       found!

# Dirty Log ioctl (KVM_GET_DIRTY_LOG)
QEMU ioctl(KVM_GET_DIRTY_LOG)
      │
      ▼
┌──────────────────────────┐
│ kvm_dev_ioctl_get_dirty_log │
│   + busy++ to block config │
│   + copy dirty_bitmap to user │
│   + clear bitmap if any dirty │
│   + remove write access via    │
│     kvm_mmu_slot_remove_write_access() │
│   + TLB flush per vcpu        │
└──────────────────────────┘


# Emulator Flow (emulate_instruction())
Guest triggers #PF or intercept
        │
        ▼
x86_emulate_memop()
        │
        ├─ emulator_read_emulated()
        │      gva_to_gpa()
        │      gfn_to_memslot()
        │      kmap_atomic(page)
        │
        ├─ do memory load/store
        │
        └─ may set mmio_needed if unmapped


# ioctl Dispatcher Tree
kvm_dev_ioctl()
 ├── KVM_GET_API_VERSION
 ├── KVM_CREATE_VCPU
 ├── KVM_RUN
 ├── KVM_GET/SET_REGS
 ├── KVM_GET/SET_SREGS
 ├── KVM_TRANSLATE
 ├── KVM_INTERRUPT
 ├── KVM_DEBUG_GUEST
 ├── KVM_SET_MEMORY_REGION
 ├── KVM_GET_DIRTY_LOG
 ├── KVM_GET/SET_MSRS
 ├── KVM_GET_MSR_INDEX_LIST
 └── default: -EINVAL


# Initialization / Teardown Flow
module_init(kvm_init)
   │
   ├─ kvm_init_debug()
   │    create /sys/kernel/debug/kvm/*
   │
   ├─ kvm_init_msr_list()
   │    detect supported MSRs
   │
   ├─ alloc bad_page for faults
   │
   └─ register misc device /dev/kvm
        ↑
        │
   misc_register(&kvm_dev)


module_exit(kvm_exit)
   │
   ├─ deregister misc device
   ├─ free bad_page
   ├─ remove debugfs entries
   └─ disable hardware virtualization


# VCPU ↔ MMU ↔ memslot Interaction Summary
Guest CPU executes → #PF → KVM MMU handles
    │
    ├─ guest GVA → guest PT → GPA
    │
    ├─ GPA → memslot → struct page → HPA
    │
    ├─ HPA installed in shadow PTE
    │
    └─ guest resumes with GVA→HPA mapping cached


# Simplified All-Subsystem Flow
┌────────────────────────────────────────────────────────────────────────┐
│                           QEMU /dev/kvm ioctl                          │
│                                                                        │
│ open → ioctl(KVM_CREATE_VM)                                            │
│        ├─ alloc struct kvm                                             │
│        ├─ ioctl(KVM_SET_MEMORY_REGION) → alloc guest pages             │
│        ├─ ioctl(KVM_CREATE_VCPU) → alloc vcpu + mmu                    │
│        ├─ ioctl(KVM_RUN)                                               │
│        │     → kvm_arch_ops->run() → vm-entry → guest run              │
│        │     ← VMEXIT (#PF,IO,etc) → handle_exit → fill kvm_run        │
│        ├─ ioctl(KVM_GET/SET_REGS,SREGS,MSRS,etc.)                      │
│        └─ ioctl(KVM_GET_DIRTY_LOG, INTERRUPT, etc.)                    │
│                                                                        │
└────────────────────────────────────────────────────────────────────────┘


# grok
1. Module Init / Exit
┌─────────────────┐
│  kvm_init()     │
└─────┬───────────┘
      │
      ├─ kvm_init_debug()               ──► debugfs /kvm/*
      ├─ kvm_init_msr_list()             ──► msrs_to_save[]
      ├─ alloc bad_page ──► bad_page_address
      │
      └─► return 0
                │
                ▼
        kvm_init_arch(ops, module)
                │
      ┌─────────┴──────────┐
      │ hardware_enable all CPUs │
      └─────────┬──────────┘
                │
                ▼
        misc_register(/dev/kvm)
                │
                ▼
        register_reboot_notifier()

2. /dev/kvm open → KVM_CREATE_VCPU → KVM_RUN
/dev/kvm open
   │
   ▼
kvm_dev_open()
   │
   └─► kzalloc(struct kvm)
          │
          └─► init vcpus[0..KVM_MAX_VCPUS]
                 mutex_init(&vcpu->mutex)

/dev/kvm ioctl(KVM_CREATE_VCPU, n)
   │
   ▼
kvm_dev_ioctl_create_vcpu()
   │
   ├─► vcpu->vmcs = NULL ? → EEXIST
   ├─► kvm_arch_ops->vcpu_create()
   ├─► kvm_mmu_create()
   ├─► kvm_mmu_setup()
   └─► return fd (vcpu)

/dev/kvm ioctl(KVM_RUN)
   │
   ▼
kvm_dev_ioctl_run()
   │
   ├─► vcpu_load() → mutex_lock(&vcpu->mutex)
   ├─► kvm_arch_ops->run(vcpu, kvm_run)
   └─► vcpu_put() → mutex_unlock()



3. Memory Slot Management (KVM_SET_MEMORY_REGION)
ioctl(KVM_SET_MEMORY_REGION)
   │
   ▼
kvm_dev_ioctl_set_memory_region()
   │
   ├─► sanity checks (align, overlap)
   ├─► spin_lock(&kvm->lock)
   ├─► detect race → memory_config_version
   ├─► spin_unlock()
   ├─► vmalloc phys_mem[] + alloc_page() for each
   ├─► vmalloc dirty_bitmap if LOG_DIRTY
   ├─► spin_lock again
   ├─► *memslot = new
   ├─► kvm->memory_config_version++
   ├─► for each vcpu: kvm_mmu_reset_context()
   └─► free old slot


4. Guest Page Fault → MMIO / Emulation
guest page fault
   │
   ▼
kvm_arch_ops->handle_exit()
   │
   └─► emulate_instruction()
          │
          ├─► x86_emulate_memop() → emulate_ops
          │       │
          │       ├─► read_emulated() → gva_to_gpa()
          │       │        │
          │       │        └─► UNMAPPED_GVA → mmio_needed=1
          │       └─► write_emulated() → mmio_needed=1
          │
          └─► return EMULATE_DO_MMIO
                  │
                  ▼
            kvm_run->exit_reason = KVM_EXIT_MMIO
            copy mmio_phys_addr / data / len / is_write
            → userspace handles → KVM_RUN again with mmio_completed


5. MSR Access Flow
ioctl(KVM_GET_MSRS / KVM_SET_MSRS)
   │
   ▼
msr_io()
   │
   ├─► copy_from_user(msrs + entries)
   ├─► __msr_io()
   │      │
   │      ├─► vcpu_load()
   │      └─► get_msr() → kvm_arch_ops->get_msr()
   │             or
   │             set_msr() → kvm_arch_ops->set_msr()
   └─► copy_to_user() (for GET)


6. CR0/CR3/CR4 Write Flow
guest MOV to CR0/CR3/CR4
   │
   ▼
set_cr0/cr3/cr4()
   │
   ├─► reserved bits? → inject #GP
   ├─► invalid combinations? → #GP
   ├─► PAE/PDPT checks → load_pdptrs()
   ├─► kvm_arch_ops->set_crX()
   └─► kvm_mmu_reset_context()


7. Interrupt Injection
ioctl(KVM_INTERRUPT)
   │
   ▼
kvm_dev_ioctl_interrupt()
   │
   └─► set_bit(irq, vcpu->irq_pending)
        set_bit(word, &vcpu->irq_summary)
        → next VM-entry delivers IRQ


8. Module Exit / Reboot
reboot → kvm_reboot()
   │
   └─► on_each_cpu(hardware_disable)

kvm_exit()
   │
   ├─► misc_deregister(/dev/kvm)
   ├─► hardware_disable all CPUs
   └─► hardware_unsetup()

