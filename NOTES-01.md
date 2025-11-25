# Fast path (SYSCALL / SYSRETQ)
user → syscall
    ↓
ENTRY(system_call)
    swapgs → switch GS to kernel base
    save regs → pt_regs
    lookup sys_call_table[rax]
    call sys_*()
    ↓
ret_from_sys_call:
    check thread_info->flags
      ├─ none → sysretq (return to user)
      ├─ NEED_RESCHED → schedule()
      └─ SIGPENDING / NOTIFY → do_notify_resume()
             ↓
          do_signal() → handle_signal()
             ↓
          setup_rt_frame()
             ↓
          sysretq → user handler()





# Slow path (IRETQ)
Used when SYSRET can’t be used (ptrace, 32-bit emu, signal return):
int_ret_from_sys_call:
    check flags
    ├─ NEED_RESCHED → schedule()
    └─ SIGPENDING / NOTIFY → do_notify_resume()
    ↓
iretq → user


# Interrupt Flow
Interrupts = asynchronous hardware events.
CPU interrupt →
ENTRY(common_interrupt)
    SAVE_ARGS
    call do_IRQ()                 ← generic C IRQ handler
    ↓
ret_from_intr:
    check if from kernel or user
      ├─ kernel → maybe preempt_schedule_irq()
      └─ user   → retint_check
                     ├─ NEED_RESCHED → schedule()
                     └─ SIGPENDING / NOTIFY → do_notify_resume()
    ↓
iretq → resume previous context


# APIC Interrupt Vectors
ENTRY(reschedule_interrupt)       → smp_reschedule_interrupt()
ENTRY(call_function_interrupt)    → smp_call_function_interrupt()
ENTRY(thermal_interrupt)          → smp_thermal_interrupt()
ENTRY(threshold_interrupt)        → mce_threshold_interrupt()
ENTRY(error_interrupt)            → smp_error_interrupt()
ENTRY(spurious_interrupt)         → smp_spurious_interrupt()
ENTRY(apic_timer_interrupt)       → smp_apic_timer_interrupt()


# Exception / Fault Flow
Example — Page Fault

KPROBE_ENTRY(page_fault)
    errorentry do_page_fault
KPROBE_END(page_fault)

CPU → #PF trap
    ↓
errorentry do_page_fault
    SAVE_ALL
    call do_page_fault()
    ↓
retint_check → maybe do_notify_resume() before iretq


# “Paranoid” Exceptions (NMI, Double Fault, Debug)
ENTRY(nmi)
    paranoidentry do_nmi, 0, 0
    paranoidexit0

ENTRY(double_fault)
    paranoidentry do_double_fault
    paranoid_exit1


CPU NMI or #DF
    ↓
SAVE_ALL minimal state
maybe swapgs
call C handler (do_nmi/do_double_fault)
    ↓
paranoidexit:
    check user/kernel mode
      ├─ kernel → swapgs → iretq
      └─ user   → call do_notify_resume()


# Signal Return Stub
ENTRY(stub_rt_sigreturn)
    SAVE_REST
    call sys_rt_sigreturn()
    jmp int_ret_from_sys_call


# Kernel Thread and Fork Flows
ENTRY(kernel_thread)

User (in kernel) calls kernel_thread(fn,arg,flags)
    ↓
FAKE_STACK_FRAME
SAVE_ALL
call do_fork()
RESTORE_ALL
UNFAKE_STACK_FRAME
ret


ENTRY(ret_from_fork)
Executed in the newly forked child:
ret_from_fork:
    call schedule_tail()
    RESTORE_REST
    if returning to user → ret_from_sys_call
    else → int_ret_from_sys_call


ENTRY(child_rip)
This is the function the child starts executing in kernel space:
child_rip:
    call *%rax  (the fn passed)
    call do_exit()


#Execve Flow
ENTRY(kernel_execve)
    FAKE_STACK_FRAME
    SAVE_ALL
    call sys_execve()
    RESTORE_REST
    je int_ret_from_sys_call
    ret

This sets up a new program image (and thus new user stack / signal mask).


# SoftIRQ Helper
ENTRY(call_softirq)
    incl %gs:pda_irqcount
    cmove %gs:pda_irqstackptr,%rsp
    call __do_softirq
    decl %gs:pda_irqcount
    ret

This runs deferred work (bottom halves) on the interrupt stack, invoked by hard-IRQ exit.


# Misc Helpers
ENTRY(load_gs_index) → safely reloads GS selector, used when switching contexts.
ENTRY(ptregscall_common) → generic helper that calls a C function taking struct pt_regs * (used for do_notify_resume and other stubs).


# COMBINED ASCII MAP
                     ┌────────────┐
                     │ user mode  │
                     └────┬───────┘
                          │
        ┌─────────────────┴──────────────────┐
        │                                    │
   Syscall (SYSCALL)                    Interrupt/Fault
        │                                    │
        ▼                                    ▼
ENTRY(system_call)                 common_interrupt / page_fault / nmi / …
        │                                    │
        ▼                                    ▼
sys_call_table[rax]                 C handler (do_IRQ, do_page_fault, etc.)
        │                                    │
        ▼                                    ▼
ret_from_sys_call                   ret_from_intr / error_exit / paranoid_exit
        │                                    │
   check thread_info->flags   ←──────────────┘
        │
        ├─ none → sysretq / iretq → user
        ├─ NEED_RESCHED → schedule()
        └─ SIGPENDING / NOTIFY → do_notify_resume()
                                      │
                                      ▼
                                do_signal()
                                      │
                                      ▼
                             setup_rt_frame()
                                      │
                                      ▼
                                 iretq/sysretq
                                      │
                                      ▼
                                user signal handler()







# summary
arch/x86_64/kernel/entry.S is the CPU event dispatcher.
Every possible entry or exit between user and kernel passes through it.
ategory             Label                           Ends up calling
Syscall	            system_call	                    sys_* →  maybe do_notify_resume()
Interrupt	        common_interrupt	            do_IRQ → do_notify_resume()
Fault/Exception	    errorentry / zeroentry	        do_page_fault, etc.
NMI / Double Fault	paranoidentry	                do_nmi, do_double_fault
Signal return	    stub_rt_sigreturn	            sys_rt_sigreturn()
Kernel thread	    kernel_thread, ret_from_fork	do_fork(), schedule_tail()
Exec	            kernel_execve	                sys_execve()
SoftIRQ	            call_softirq	                __do_softirq()




