# ====================================================================================
#  AMD SVM EXIT CODES (Linux 2.6.20) — INSTRUCTION ↔ VMEXIT MAPPING
# ====================================================================================

LEGEND:
- VMEXIT occurs ONLY if the corresponding intercept bit is enabled in the VMCB
- CRx/DRx exits are due to MOV to/from control/debug registers
- Event exits are asynchronous CPU events
- Instruction exits are synchronous to instruction execution

------------------------------------------------------------------------------------
 EXIT CODE                      INSTRUCTION / EVENT                WHY IT EXITS
------------------------------------------------------------------------------------

--- CONTROL REGISTER (CRx) ACCESSES -----------------------------------------------

 SVM_EXIT_READ_CR0   (0x000)     mov %cr0, reg           Paging, PE, WP control
 SVM_EXIT_READ_CR3   (0x003)     mov %cr3, reg           Address space base
 SVM_EXIT_READ_CR4   (0x004)     mov %cr4, reg           PAE, PSE, VMXE, SMEP
 SVM_EXIT_READ_CR8   (0x008)     mov %cr8, reg           Interrupt priority (TPR)

 SVM_EXIT_WRITE_CR0  (0x010)     mov reg, %cr0           Enable/disable paging
 SVM_EXIT_WRITE_CR3  (0x013)     mov reg, %cr3           Context switch
 SVM_EXIT_WRITE_CR4  (0x014)     mov reg, %cr4           MMU feature control
 SVM_EXIT_WRITE_CR8  (0x018)     mov reg, %cr8           IRQ priority changes

------------------------------------------------------------------------------------
 DEBUG REGISTER (DRx) ACCESSES -----------------------------------------------------

 SVM_EXIT_READ_DR0   (0x020)     mov %dr0, reg           HW breakpoints
 SVM_EXIT_READ_DR1   (0x021)     mov %dr1, reg
 SVM_EXIT_READ_DR2   (0x022)     mov %dr2, reg
 SVM_EXIT_READ_DR3   (0x023)     mov %dr3, reg
 SVM_EXIT_READ_DR4   (0x024)     mov %dr4, reg           (alias of DR6)
 SVM_EXIT_READ_DR5   (0x025)     mov %dr5, reg           (alias of DR7)
 SVM_EXIT_READ_DR6   (0x026)     mov %dr6, reg           Debug status
 SVM_EXIT_READ_DR7   (0x027)     mov %dr7, reg           Debug control

 SVM_EXIT_WRITE_DR0  (0x030)     mov reg, %dr0
 SVM_EXIT_WRITE_DR1  (0x031)     mov reg, %dr1
 SVM_EXIT_WRITE_DR2  (0x032)     mov reg, %dr2
 SVM_EXIT_WRITE_DR3  (0x033)     mov reg, %dr3
 SVM_EXIT_WRITE_DR4  (0x034)     mov reg, %dr4
 SVM_EXIT_WRITE_DR5  (0x035)     mov reg, %dr5
 SVM_EXIT_WRITE_DR6  (0x036)     mov reg, %dr6
 SVM_EXIT_WRITE_DR7  (0x037)     mov reg, %dr7

------------------------------------------------------------------------------------
 EXCEPTIONS & INTERRUPTS -----------------------------------------------------------

 SVM_EXIT_EXCP_BASE  (0x040+n)   #DE,#PF,#GP,#UD,...      CPU exception vector n
 SVM_EXIT_INTR      (0x060)     External interrupt       IRQ from PIC/APIC
 SVM_EXIT_NMI       (0x061)     Non-maskable interrupt
 SVM_EXIT_SMI       (0x062)     System Management IRQ
 SVM_EXIT_INIT      (0x063)     INIT signal              CPU reset-style event
 SVM_EXIT_VINTR     (0x064)     Virtual interrupt        Virtual IRQ injection

------------------------------------------------------------------------------------
 DESCRIPTOR TABLE & TASK STATE -----------------------------------------------------

 SVM_EXIT_CR0_SEL_WRITE (0x065)  mov to CR0 w/ selector   Mode transition

 SVM_EXIT_IDTR_READ  (0x066)     sidt                    Read IDT base/limit
 SVM_EXIT_GDTR_READ  (0x067)     sgdt                    Read GDT base/limit
 SVM_EXIT_LDTR_READ  (0x068)     sldt                    Read LDT selector
 SVM_EXIT_TR_READ    (0x069)     str                     Read TSS selector

 SVM_EXIT_IDTR_WRITE (0x06a)     lidt                    Load IDT
 SVM_EXIT_GDTR_WRITE (0x06b)     lgdt                    Load GDT
 SVM_EXIT_LDTR_WRITE (0x06c)     lldt                    Load LDT
 SVM_EXIT_TR_WRITE   (0x06d)     ltr                     Load TSS

 SVM_EXIT_TASK_SWITCH (0x07d)    Hardware task switch    TSS-based switch

------------------------------------------------------------------------------------
 TIME, PERFORMANCE & FLAGS ---------------------------------------------------------

 SVM_EXIT_RDTSC      (0x06e)     rdtsc                   Time virtualization
 SVM_EXIT_RDPMC      (0x06f)     rdpmc                   Performance counters
 SVM_EXIT_RDTSCP     (0x087)     rdtscp                  Serialized TSC read

 SVM_EXIT_PUSHF      (0x070)     pushf / pushfq          IF, IOPL visibility
 SVM_EXIT_POPF       (0x071)     popf / popfq            IF modification

------------------------------------------------------------------------------------
 CONTROL-FLOW / PRIVILEGE ----------------------------------------------------------

 SVM_EXIT_CPUID      (0x072)     cpuid                   Feature virtualization
 SVM_EXIT_RSM        (0x073)     rsm                     Exit from SMM
 SVM_EXIT_IRET       (0x074)     iret / iretq            Privilege return
 SVM_EXIT_SWINT      (0x075)     int n                   Software interrupt
 SVM_EXIT_ICEBP      (0x088)     icebp                   Debug trap

------------------------------------------------------------------------------------
 MEMORY, CACHE & TLB ---------------------------------------------------------------

 SVM_EXIT_INVD       (0x076)     invd                    Cache invalidate
 SVM_EXIT_WBINVD     (0x089)     wbinvd                  Writeback + invalidate
 SVM_EXIT_INVLPG     (0x079)     invlpg [addr]           Guest TLB flush
 SVM_EXIT_INVLPGA    (0x07a)     invlpga                 ASID-based flush

------------------------------------------------------------------------------------
 CPU POWER & SCHEDULING ------------------------------------------------------------

 SVM_EXIT_PAUSE      (0x077)     pause                   Spinlock hint
 SVM_EXIT_HLT        (0x078)     hlt                     CPU idle
 SVM_EXIT_SHUTDOWN   (0x07f)     Triple fault            Guest death

------------------------------------------------------------------------------------
 I/O & MSR ACCESS ------------------------------------------------------------------

 SVM_EXIT_IOIO       (0x07b)     in / out                 Port I/O virtualization
 SVM_EXIT_MSR        (0x07c)     rdmsr / wrmsr            CPU personality control

------------------------------------------------------------------------------------
 SVM-SPECIFIC (HYPERVISOR BOUNDARY) ------------------------------------------------

 SVM_EXIT_VMRUN      (0x080)     vmrun                   Enter guest
 SVM_EXIT_VMMCALL    (0x081)     vmmcall                 Hypercall
 SVM_EXIT_VMLOAD     (0x082)     vmload                  Load guest state
 SVM_EXIT_VMSAVE     (0x083)     vmsave                  Save guest state
 SVM_EXIT_STGI       (0x084)     stgi                    Enable global IRQs
 SVM_EXIT_CLGI       (0x085)     clgi                    Disable global IRQs
 SVM_EXIT_SKINIT     (0x086)     skinit                  Secure init

------------------------------------------------------------------------------------
 NESTED PAGING ---------------------------------------------------------------------

 SVM_EXIT_NPF        (0x400)     Nested Page Fault        GPA → HPA translation

====================================================================================
 END OF TABLE
====================================================================================

