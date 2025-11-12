┌────────────────────────────────────────────────────────────────────────────────────┐
│                              SVM vs VMX ARCHITECTURE COMPARISON                   │
└────────────────────────────────────────────────────────────────────────────────────┘

1. Hardware Enable/Disable Flow
┌──────────────────────┐                    ┌──────────────────────┐
│       AMD SVM        │                    │      Intel VMX       │
├──────────────────────┤                    ├──────────────────────┤
│ MSR_EFER.SVME = 1    │                    │ VMXON region         │
│ MSR_VM_HSAVE_PA      │                    │ → vmxon()            │
│ per-cpu save_area    │                    │ IA32_VMX_BASIC       │
│ wrmsrl()             │                    │ vmx_hardware_enable()│
│ CLGI/STGI            │                    │ no per-cpu state     │
└──────────────────────┘                    └──────────────────────┘

2. VCPU Entry Point
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ VMRUN (one insn)     │                    │ VMLAUNCH/VMRESUME    │
│ VMLOAD/VMSAVE        │                    │ VMREAD/VMWRITE       │
│ Single VMCB page     │                    │ VMCS per vCPU        │
│ ASID in VMCB         │                    │ EPT/VPID in VMCS     │
│ TLB_CONTROL_*        │                    │ INVEPT/INVVPID       │
└──────────────────────┘                    └──────────────────────┘

3. Guest State Save/Restore
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ VMCB->save.*         │                    │ VMCS fields          │
│ RIP, RFLAGS, RSP     │                    │ Guest RIP, RSP       │
│ RAX always saved     │                    │ RAX not auto-saved   │
│ CS, DS, ES, FS, GS   │                    │ Same                 │
│ TR, LDTR             │                    │ Same                 │
│ CR0/CR3/CR4          │                    │ Same                 │
│ EFER, STAR, LSTAR    │                    │ VMX has no LSTAR     │
└──────────────────────┘                    └──────────────────────┘

4. TLB / Address Space Management
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ ASID (1..max_asid)   │                    │ VPID (1..65535)      │
│ force_new_asid()     │                    │ vpid++               │
│ TLB_CONTROL_FLUSH_ALL_ASID│                │ INVEPT single-context│
│ INVLPGA gva, asid    │                    │ INVVPID individual   │
│ No EPT in 2006       │                    │ No EPT in 2006       │
│ Guest CR3 → ASID flush│                   │ Guest CR3 → VPID flush│
└──────────────────────┘                    └──────────────────────┘

5. Intercepts / Exit Reasons
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ exit_code (32-bit)   │                    │ exit_reason (16-bit) │
│ 80+ exit codes       │                    │ 50+ exit reasons     │
│ SVM_EXIT_IOIO        │                    │ EXIT_REASON_IO_INSTRUCTION│
│ SVM_EXIT_MSR         │                    │ EXIT_REASON_MSR_READ/WRITE│
│ SVM_EXIT_CPUID       │                    │ EXIT_REASON_CPUID    │
│ SVM_EXIT_HLT         │                    │ EXIT_REASON_HLT      │
│ SVM_EXIT_SHUTDOWN    │                    │ EXIT_REASON_TRIPLE_FAULT│
│ SVM_EXIT_TASK_SWITCH │                    │ EXIT_REASON_TASK_SWITCH│
│ SVM_EXIT_VINTR       │                    │ EXIT_REASON_INTERRUPT_WINDOW│
└──────────────────────┘                    └──────────────────────┘

6. I/O Interception
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ exit_info_1:         │                    │ IO instruction:      │
│  port, size, type,   │                    │  port in qualifier   │
│  string, rep         │                    │  size, in/out, string│
│ exit_info_2 = next_rip│                   │  no next_rip         │
│ IOPM bitmap (full)   │                    │  IO bitmap A/B       │
└──────────────────────┘                    └──────────────────────┘

7. MSR Interception
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ MSRPM bitmap         │                    │ MSR bitmap (read/write)│
│ 2 bits per MSR       │                    │ 1 bit per MSR        │
│ covers 3 ranges      │                    │ full 32-bit range    │
│ exit_info_1 = write  │                    │ separate RDMSR/WRMSR │
└──────────────────────┘                    └──────────────────────┘

8. Interrupt Injection
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ event_inj + event_inj_err│                 │ VM_ENTRY_INTR_INFO   │
│ V_IRQ_MASK in int_ctl│                    │ VM_ENTRY_INTR_INFO   │
│ VINTR intercept      │                    │ INTERRUPT_WINDOW     │
│ pop_irq/push_irq     │                    │ Same logic           │
└──────────────────────┘                    └──────────────────────┘

9. Page Fault Handling
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ exit_info_2 = fault addr│                 │ GUEST_PHYSICAL_ADDRESS│
│ exit_info_1 = error_code│                 │ EXIT_QUALIFICATION   │
│ inject via event_inj │                    │ vmx_inject_page_fault│
│ Same MMU code        │                    │ Same MMU code        │
└──────────────────────┘                    └──────────────────────┘

10. Shadow Paging (2006: No NPT/EPT)
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ Identical MMU        │                    │ Identical MMU        │
│ mmu.c shared         │                    │ mmu.c shared         │
│ rmap, pte_chain      │                    │ rmap, pte_chain      │
│ ASID/VPID flush      │                    │ ASID/VPID flush      │
└──────────────────────┘                    └──────────────────────┘

11. Guest Debug (DR registers)
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ DR6/DR7 in VMCB      │                    │ GUEST_DR7            │
│ GD bit handling      │                    │ MOV-DR exit          │
│ Manual save/restore  │                    │ VMX handles auto     │
└──────────────────────┘                    └──────────────────────┘

12. FPU Handling
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ fx_save/fx_restore   │                    │ Identical            │
│ host_fx_image        │                    │ host_fx_image        │
│ guest_fx_image       │                    │ guest_fx_image       │
└──────────────────────┘                    └──────────────────────┘

13. Exit Handler Complexity
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ ~80 handlers         │                    │ ~50 handlers         │
│ Many "emulate_on_interception"│            │ More specific handlers│
│ io_interception()    │                    │ handle_io()          │
│ pf_interception()    │                    │ handle_exception()   │
└──────────────────────┘                    └──────────────────────┘

14. Code Size (Linux 2.6.20)
┌──────────────────────┐                    ┌──────────────────────┐
│       SVM            │                    │        VMX           │
├──────────────────────┤                    ├──────────────────────┤
│ svm.c: ~2800 LOC     │                    │ vmx.c: ~3200 LOC     │
│ Simpler entry        │                    │ Complex VMCS         │
│ More bitmap config   │                    │ More VMCS fields     │
└──────────────────────┘                    └──────────────────────┘

15. Summary Table
┌──────────────────────┬──────────────────────┬──────────────────────┐
│ Feature              │ AMD SVM (2006)       │ Intel VMX (2006)     │
├──────────────────────┼──────────────────────┼──────────────────────┤
│ Entry instruction    │ VMRUN                │ VMLAUNCH/VMRESUME    │
│ State structure      │ VMCB (one page)      │ VMCS (cached)        │
│ TLB virtualization   │ ASID                 │ VPID (later)         │
│ I/O bitmaps          │ IOPM full            │ IO bitmap A/B        │
│ MSR bitmaps          │ MSRPM 2-bit          │ MSR bitmap 1-bit     │
│ Nested paging        │ No (NPT in 2008)     │ No (EPT in 2008)     │
│ Interrupt window     │ VINTR intercept      │ INTERRUPT_WINDOW     │
│ Exit info            │ exit_info_1/2        │ exit_qualification   │
│ Host state save      │ Manual               │ Mostly automatic     │
│ Guest CR3 change     │ ASID bump            │ VPID bump            │
│ Debug registers      │ Manual DR6/DR7       │ MOV-DR exit          │
└──────────────────────┴──────────────────────┴──────────────────────┘
