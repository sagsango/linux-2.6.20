linux-2.6.20 ❱❱❱ find * | grep kthread
11524:drivers/media/video/msp3400-kthreads.c
17234:include/linux/kthread.h
19409:kernel/kthread.c



./arch/i386/kernel/process.c
/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
    struct pt_regs regs;

    memset(&regs, 0, sizeof(regs));

    regs.ebx = (unsigned long) fn;
    regs.edx = (unsigned long) arg;

    regs.xds = __USER_DS;
    regs.xes = __USER_DS;
    regs.xgs = __KERNEL_PDA;
    regs.orig_eax = -1;
    regs.eip = (unsigned long) kernel_thread_helper;
    regs.xcs = __KERNEL_CS | get_kernel_rpl();
    regs.eflags = X86_EFLAGS_IF | X86_EFLAGS_SF | X86_EFLAGS_PF | 0x2;

    /* Ok, create the new process.. */
    return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL);
}
EXPORT_SYMBOL(kernel_thread);
