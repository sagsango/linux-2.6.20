# Top-level layout
QEMU process
│  ioctl(KVM_RUN)
▼
+----------------------------+
| KVM core (kvm_main.c)      |
|  ↳ vcpu->arch.run()        |
+----------------------------+
             │
             ▼
+--------------------------------------+
| svm_vcpu_run()                       |
|  ↳ enter guest (VMRUN)               |
|  ↳ on VMEXIT → handle_exit()         |
+--------------------------------------+
             │
             ▼
+-----------------------+
| svm_exit_handlers[]   |
|   per-exit dispatcher |
+-----------------------+
             │
             ▼
     emulate / mmio /
     page-fault / io /
     cpuid / msr …


# Hardware enable/disable path
module_init(svm_init)
    │
    ▼
svm_hardware_setup()
    │ alloc IOPM, MSRPM pages
    │ set MSR interception bitmap
    │ for_each_cpu → svm_cpu_init()
    ▼
svm_hardware_enable()
    │ rdmsr/ wrmsr MSR_EFER |= SVME
    │ wrmsr MSR_VM_HSAVE_PA = save_area
    ▼
CPU now SVM-capable


svm_hardware_disable()
    │ wrmsr(MSR_EFER &~SVME)
    │ free save_area
svm_hardware_unsetup()
    │ free IOPM/MSRPM
module_exit(svm_exit)


# VCPU creation & VMCB init
svm_create_vcpu()
    │ alloc vcpu->svm
    │ alloc one page for VMCB
    │ init_vmcb():
    │   ├ intercept bits (CRx, DRx, IOIO, MSR…)
    │   ├ control->iopm_base_pa = iopm_base
    │   ├ control->msrpm_base_pa = msrpm_base
    │   └ initialize save area (segments, CR0, RIP=FFF0…)
    ▼
vcpu ready



# Run loop (main entry from KVM_RUN)
svm_vcpu_run():
┌──────────────────────────────────────────┐
│ do_interrupt_requests()  (check queued)  │
│ clgi()        — clear global-interrupts  │
│ pre_svm_run() — assign new ASID if need  │
│ save_host_msrs()                        │
│ fx_save(host) / fx_restore(guest)       │
│
│   +------------------------------+
│   |  ASM:                        |
│   |  VMLOAD  [vmcb_pa]           |
│   |  VMRUN                       |  ← enters guest
│   |  VMSAVE [vmcb_pa]            |
│   +------------------------------+
│
│ fx_save(guest)/fx_restore(host)          │
│ restore debug regs, CR2/DR6/DR7          │
│ reload_tss(), stgi(), kvm_reput_irq()    │
│                                           │
│ if exit_code==SVM_EXIT_ERR → fail_entry   │
│ else handle_exit(vcpu, kvm_run)           │
│   ↳ dispatch to svm_exit_handlers[]       │
│   ↳ may re-enter guest (goto again)      │
└──────────────────────────────────────────┘


# VMEXIT decode & dispatch
handle_exit()
   │ exit_code = vmcb->control.exit_code
   │ if >table or null → UNKNOWN
   ▼
   svm_exit_handlers[exit_code](vcpu,kvm_run)

svm_exit_handlers:
 ├ PF_VECTOR → pf_interception()
 ├ SVM_EXIT_IOIO → io_interception()
 ├ SVM_EXIT_MSR  → msr_interception()
 ├ SVM_EXIT_CPUID→ cpuid_interception()
 ├ SVM_EXIT_HLT  → halt_interception()
 ├ SVM_EXIT_SHUTDOWN → shutdown_interception()
 ├ SVM_EXIT_VINTR → interrupt_window_interception()
 └ default → emulate_on_interception()


# Page-fault intercept path
PF VMEXIT
   ▼
pf_interception()
   │ read exit_info_2 = fault_address
   │ read exit_info_1 = error_code
   │ spin_lock(kvm)
   │ r = kvm_mmu_page_fault(vcpu, addr, err)
   │ if r<0 → fatal
   │ if r==0 → handled, resume
   │ else emulate_instruction()
   ▼
   may set kvm_run->exit_reason=MMIO

# IO intercept path 
IOIO VMEXIT
   ▼
io_interception()
   │ parse exit_info_1 (port,size,dir,string,rep)
   │ if string:
   │    addr_mask = io_address()
   │    compute vcpu->regs{RSI/RDI} + segment base
   │ fill kvm_run->io.*
   │ user-space will perform IO

# MSR intercepts
MSR VMEXIT
   ▼
msr_interception()
   │ if exit_info_1==1 → WRMSR
   │   ↳ wrmsr_interception()
   │ else → RDMSR
   │   ↳ rdmsr_interception()
wrmsr_interception:
   ecx = RCX, data=(RDX: RAX)
   if svm_set_msr() fails → inject #GP
rdmsr_interception:
   if svm_get_msr() fails → inject #GP
   else write RAX/RDX, skip instruction



# Interrupt delivery
do_interrupt_requests():
    if IF=1 && not blocked by shadow
        kvm_do_inject_irq()
        control->int_vector = pop_irq()
        control->int_ctl |= V_IRQ_MASK
    else if blocked:
        control->intercept |= INTERCEPT_VINTR

kvm_reput_irq()   → push back pending if not delivered
post_kvm_run_save() → update kvm_run flags


# ASID / TLB management
pre_svm_run():
    if (vcpu->cpu changed ||
        asid_generation mismatch)
         new_asid()
new_asid():
    if next_asid>max_asid:
        ++asid_generation
        next_asid=1
        vmcb->control.tlb_ctl=FLUSH_ALL_ASID
    vmcb->control.asid = next_asid++

svm_flush_tlb() → force_new_asid()
svm_set_cr3()   → vmcb->save.cr3=root; force_new_asid()



# VMRUN/VMEXIT hardware registers
per-CPU MSRs
 ├ MSR_EFER.SVME=1
 ├ MSR_VM_HSAVE_PA → per-CPU save-area for host-state
per-VCPU:
 ├ vmcb (1 page) – control + save
 ├ iopm_base / msrpm_base (shared tables)
 └ ASID (Address-space identifier)


# VMCB layout inside
+-----------------------------------------------------+
| CONTROL AREA                                         |
|  ├ intercept_cr_read/write                           |
|  ├ intercept_dr_read/write                           |
|  ├ intercept_exceptions (bit for PF)                 |
|  ├ intercept (bitmap for IO,MSR,CPUID,HLT,...)       |
|  ├ iopm_base_pa / msrpm_base_pa                      |
|  ├ asid, tlb_ctl, int_ctl, event_inj,…               |
|  └ exit_code / exit_info_1 / exit_info_2             |
|------------------------------------------------------|
| SAVE AREA                                            |
|  ├ segment registers (cs,ds,es,fs,gs,ss,tr,ldtr)     |
|  ├ gdtr/idtr                                         |
|  ├ rax..rflags,rip,cr0..cr4,dr6,dr7,cr2             |
|  ├ efer, star, lstar, cstar, sfmask…                 |
|  └ tsc_offset                                        |
+------------------------------------------------------+

# Guest-run cycle (full)
┌───────────────────────────────────────────────────────────┐
│       HOST (QEMU/KVM)                                     │
│  ioctl(KVM_RUN)                                           │
│  → svm_vcpu_run()                                         │
│     ├ save host state (MSRs, FPU, DRs)                    │
│     ├ clgi(); pre_svm_run();                              │
│     ├ VMLOAD; VMRUN; (enter guest)                        │
│     └ VMSAVE; (on VMEXIT)                                 │
│        ├ restore host state                                │
│        ├ decode exit_code                                 │
│        ├ handle_exit() → svm_exit_handlers[exit_code]     │
│        │     ↳ pf_interception / io_interception / ...    │
│        │     ↳ may call emulate_instruction()             │
│        └ if handled→re-enter guest, else → user space     │
└───────────────────────────────────────────────────────────┘


# grok
1. SVM Hardware Detection & Global Setup
┌─────────────────────────────────────────────┐
│               svm_hardware_setup()          │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          alloc_pages(IOPM) → iopm_base
                     │
                     ▼
          alloc_pages(MSRPM) → msrpm_base
                     │
                     ▼
          set_msr_interception() for:
          ├─ MSR_GS_BASE, FS_BASE
          ├─ MSR_LSTAR, CSTAR, SYSCALL_MASK
          ├─ MSR_K6_STAR
          └─ SYSENTER MSRs
                     │
                     ▼
          for_each_online_cpu → svm_cpu_init()
                     │
               ┌─────┴───────┐
               │ alloc svm_data│ → kzalloc + save_area page
               └──────┬───────┘
                     │
                     ▼
          per_cpu(svm_data, cpu) = svm_data

2. Per-CPU Enable/Disable (Hotplug)
┌─────────────────────────────────────────────┐
│           svm_hardware_enable()             │
└────────────────────┬────────────────────────┘
                     │
                     ▼
              has_svm()? → CPUID 8000000A
                     │
               ┌─────┴──────┐
               │   NO       │ → EOPNOTSUPP
               └─────┬──────┘
                     │
                     ▼
          svm_data = per_cpu(svm_data)
                     │
                     ▼
          wrmsrl(MSR_EFER, EFER | SVME)
                     │
                     ▼
          wrmsrl(MSR_VM_HSAVE_PA, save_area_pfn)
                     │
                     ▼
          asid_generation = 1
          next_asid = max_asid + 1

3. VCPU Creation (svm_create_vcpu)
┌─────────────────────────────────────────────┐
│              svm_create_vcpu()              │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          vcpu->svm = kzalloc(struct vcpu_svm)
                     │
                     ▼
          alloc_page() → VMCB page
                     │
                     ▼
          vmcb_pa = page_to_pfn << PAGE_SHIFT
                     │
                     ▼
          memset(vmcb, 0, PAGE_SIZE)
                     │
                     ▼
          init_vmcb():
          ├─ intercept CR0/CR3/CR4 read/write
          ├─ intercept DR0–DR7
          ├─ intercept PF, CPUID, HLT, IO, MSR, etc.
          ├─ iopm_base_pa = iopm_base
          ├─ msrpm_base_pa = msrpm_base
          ├─ tsc_offset = -rdtscll()
          ├─ init_seg(CS, DS, ES, FS, GS, SS)
          ├─ CS = 0xf000, base=0xffff0000
          ├─ CR0 = 0x60000010 | PG
          ├─ CR4 = PAE
          └─ RIP = 0xfff0
                     │
                     ▼
          fx_init(vcpu)


4. VCPU Entry – svm_vcpu_run() (Main Loop)
┌─────────────────────────────────────────────┐
│               svm_vcpu_run()                │
└────────────────────┬────────────────────────┘
                     │
               again: ◄──────────────────────┘
                     │
                     ▼
          do_interrupt_requests()
                     │
                     ▼
          clgi() → disable host interrupts
                     │
                     ▼
          pre_svm_run():
          ├─ new_asid() if CPU changed or generation bump
          └─ TLB_CONTROL_DO_NOTHING
                     │
                     ▼
          save_host_msrs(), FS/GS/LDT, CR2, DR6/7
                     │
                     ▼
          fx_save(host_fx) → fx_restore(guest_fx)
                     │
                     ▼
          ASM: push GPRs → VMLOAD → VMRUN → VMSAVE → pop GPRs
                     │
                     ▼
          fx_save(guest_fx) → fx_restore(host_fx)
                     │
                     ▼
          restore host DR6/7, CR2, FS/GS/LDT, MSRs
                     │
                     ▼
          reload_tss() → load_TR_desc()
                     │
                     ▼
          stgi() → re-enable host interrupts
                     │
                     ▼
          kvm_reput_irq() → push back pending IRQ if any
                     │
                     ▼
          handle_exit():
               exit_code → svm_exit_handlers[exit_code]()
                     │
               ┌─────┴──────┐
               │ r > 0 ?    │ → continue guest
               └─────┬──────┘
                     │
               ┌─────┴──────┐
               │ signal?    │ → KVM_EXIT_INTR
               └─────┬──────┘
                     │
               ┌─────┴──────┐
               │ IRQ window │ → KVM_EXIT_IRQ_WINDOW_OPEN
               └─────┬──────┘
                     │
                     ▼
               kvm_resched() → goto again


5. Exit Handler Dispatch Table (svm_exit_handlers[])
┌─────────────────────────────────────────────┐
│              handle_exit()                  │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          exit_code = vmcb->control.exit_code
                     │
          ┌──────────┴───────────┐
          │ SVM_EXIT_READ_CR0..4  │ → emulate_on_interception
          │ SVM_EXIT_WRITE_CR0..4 │ → emulate_on_interception
          │ SVM_EXIT_READ/WRITE_DR │ → emulate_on_interception
          │ SVM_EXIT_EXCP_BASE+PF  │ → pf_interception()
          │ SVM_EXIT_INTR/NMI      │ → nop_on_interception
          │ SVM_EXIT_VINTR         │ → interrupt_window_interception
          │ SVM_EXIT_CPUID         │ → cpuid_interception
          │ SVM_EXIT_HLT           │ → halt_interception
          │ SVM_EXIT_IOIO          │ → io_interception
          │ SVM_EXIT_MSR           │ → msr_interception (RDMSR/WRMSR)
          │ SVM_EXIT_SHUTDOWN      │ → shutdown_interception
          │ SVM_EXIT_TASK_SWITCH   │ → unsupported → KVM_EXIT_UNKNOWN
          │ SVM_EXIT_INVLPGA       │ → invalid_op_interception
          └────────────────────────┘

6. Page Fault Interception (pf_interception)
┌─────────────────────────────────────────────┐
│             pf_interception()               │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          fault_address = exit_info_2
          error_code     = exit_info_1
                     │
                     ▼
          kvm_mmu_page_fault() → shadow paging
                     │
               ┌─────┴──────┐
               │ r < 0 ?    │ → fail
               └─────┬──────┘
                     │
               ┌─────┴──────┐
               │ r == 0 ?   │ → retry guest
               └─────┬──────┘
                     │
                     ▼
          emulate_instruction() → MMIO?
                     │
               ┌─────┴──────┐
               │ MMIO?      │ → KVM_EXIT_MMIO
               └─────┬──────┘
                     │
                     ▼
          KVM_EXIT_UNKNOWN on emulate fail

7. I/O Interception (io_interception)
┌─────────────────────────────────────────────┐
│              io_interception()              │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          io_info = exit_info_1
          next_rip = exit_info_2
                     │
                     ▼
          kvm_run->io:
          ├─ port   = io_info >> 16
          ├─ size   = (io_info & SIZE_MASK) >> SHIFT
          ├─ in/out = IOIO_TYPE_MASK
          ├─ string = IOIO_STR_MASK
          ├─ rep    = IOIO_REP_MASK
                     │
               ┌─────┴──────┐
               │ string?    │
               └─────┬──────┘
                     │
                     ▼
          io_adress() → decode prefixes (67h, seg, etc.)
                     │
                     ▼
          address = (RDI/RSI & mask) + seg->base
                     │
                     ▼
          count = RCX if REP
                     │
                     ▼
          KVM_EXIT_IO → userspace handles PIO/MMIO

8. MSR Interception (msr_interception)
┌─────────────────────────────────────────────┐
│              msr_interception()             │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          exit_info_1 ? → WRMSR : RDMSR
                     │
          ┌──────────┴───────────┐
          │       WRMSR           │
          └──────────┬───────────┘
                     │
                     ▼
          data = RAX | (RDX << 32)
          svm_set_msr() → update VMCB fields
                     │
               ┌─────┴──────┐
               │ GP on fail │ → inject_gp()
               └────────────┘
                     │
          ┌──────────┴───────────┐
          │       RDMSR           │
          └──────────┬───────────┘
                     │
                     ▼
          svm_get_msr() → TSC (with offset), STAR, LSTAR, etc.
                     │
                     ▼
          RAX = data[31:0], RDX = data[63:32]
                     │
                     ▼
          skip_emulated_instruction() → RIP += 2

9. Interrupt Injection (do_interrupt_requests)
┌─────────────────────────────────────────────┐
│         do_interrupt_requests()             │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          interrupt_window_open =
          !(int_state & SHADOW) && RFLAGS.IF
                     │
               ┌─────┴──────┐
               │ irq_summary│
               └─────┬──────┘
                     │
                     ▼
          pop_irq() → vector
                     │
                     ▼
          control->int_vector = vector
          control->int_ctl |= V_IRQ_MASK | priority
                     │
               ┌─────┴──────┐
               │ window closed│
               └─────┬──────┘
                     │
                     ▼
          intercept |= INTERCEPT_VINTR

10. ASID & TLB Management
┌─────────────────────────────────────────────┐
│               new_asid()                    │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          if next_asid > max_asid:
               generation++
               next_asid = 1
               TLB_CONTROL_FLUSH_ALL_ASID
                     │
                     ▼
          vmcb->control.asid = next_asid++
                     │
                     ▼
          vcpu->svm->asid_generation = current
                     │
                     ▼
          force_new_asid() → generation--
                     │
                     ▼
          svm_flush_tlb() → force_new_asid()

11. CR3/CR0/CR4 Handling
┌─────────────────────────────────────────────┐
│               svm_set_cr3()                 │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          vmcb->save.cr3 = root
                     │
                     ▼
          force_new_asid() → TLB flush
                     │
                     ▼
          svm_set_cr0():
               handles LME/LMA transition
               vmcb->save.cr0 = cr0 | PG
                     │
                     ▼
          svm_set_cr4():
               vmcb->save.cr4 = cr4 | PAE

12. Segment Handling
┌─────────────────────────────────────────────┐
│             svm_set_segment()               │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          s->base, limit, selector = var
                     │
               ┌─────┴──────┐
               │ unusable?  │ → attrib = 0
               └─────┬──────┘
                     │
                     ▼
          attrib bits:
          ├─ TYPE, S, DPL, P, AVL, L, DB, G
                     │
                     ▼
          if CS → CPL = DPL

13. VCPU Destroy Flow
┌─────────────────────────────────────────────┐
│               svm_free_vcpu()               │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          __free_page(VMCB page)
                     │
                     ▼
          kfree(vcpu->svm)
                     │
                     ▼
          svm_hardware_disable():
               wrmsrl(VM_HSAVE_PA, 0)
               EFER &= ~SVME
               free save_area


14. Module Init/Exit
┌─────────────────────────────────────────────┐
│               svm_init()                    │
└────────────────────┬────────────────────────┘
                     │
                     ▼
          kvm_init_arch(&svm_arch_ops)
                     │
                     ▼
          svm_arch_ops contains:
          ├─ .vcpu_create = svm_create_vcpu
          ├─ .run         = svm_vcpu_run
          ├─ .set_cr3     = svm_set_cr3
          ├─ .tlb_flush   = svm_flush_tlb
          └─ .inject_page_fault = svm_inject_page_fault
                     │
                     ▼
          svm_exit() → kvm_exit_arch()
