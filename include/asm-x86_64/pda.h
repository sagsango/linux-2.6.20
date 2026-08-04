#ifndef X86_64_PDA_H
#define X86_64_PDA_H

#ifndef __ASSEMBLY__
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/cache.h>
#include <asm/page.h>

/* XXX: per process datastructure;
 * 	which will be pointed by the gs register
 * 	
 *
 * XXX: How GCC SSP (Stack Smashing Protection)  Works in Kernel Space
 * When a kernel function is compiled with -fstack-protector, GCC emits a
 * localized prologue and epilogue for that function: [3]
 * Prologue: GCC reads a 64-bit secret value (the "canary") from a hardcoded
 * hardware register offset and pushes it onto the kernel stack right before
 * allocating space for local buffers. [4, 5, 6] Epilogue: Before returning, GCC
 * reads the same hardcoded register offset and compares it to the value left on
 * the stack. If they do not match, it jumps to __stack_chk_fail() and throws a
 * kernel panic. [6, 7, 8] For this to function correctly, GCC expects the
 * reference canary to always live at a precise location in memory. On x86_64
 * Linux at the time, GCC hardcoded this check to look at offset 40 (0x28)
 * relative to the %gs segment register. [9, 10]
*/

/* Per processor datastructure. %gs points to it while the kernel runs */ 
struct x8664_pda {
	struct task_struct *pcurrent;	/* 0  Current process */
	unsigned long data_offset;	/* 8 Per cpu data offset from linker
					   address */
	unsigned long kernelstack;  /* 16 top of kernel stack for current */
	unsigned long oldrsp; 	    /* 24 user rsp for system call */
        int irqcount;		    /* 32 Irq nesting counter. Starts with -1 */
	int cpunumber;		    /* 36 Logical CPU number */
#ifdef CONFIG_CC_STACKPROTECTOR
	unsigned long stack_canary;	/* 40 stack canary value */
					/* gcc-ABI: this canary MUST be at
					   offset 40!!! */
#endif
	char *irqstackptr;
	int nodenumber;		    /* number of current node */
	unsigned int __softirq_pending;
	unsigned int __nmi_count;	/* number of NMI on this CPUs */
	short mmu_state;
	short isidle;
	struct mm_struct *active_mm;
	unsigned apic_timer_irqs;
} ____cacheline_aligned_in_smp;

extern struct x8664_pda *_cpu_pda[];
extern struct x8664_pda boot_cpu_pda[];

#define cpu_pda(i) (_cpu_pda[i])

/* 
 * There is no fast way to get the base address of the PDA, all the accesses
 * have to mention %fs/%gs.  So it needs to be done this Torvaldian way.
 */ 
extern void __bad_pda_field(void) __attribute__((noreturn));

/*
 * proxy_pda doesn't actually exist, but tell gcc it is accessed for
 * all PDA accesses so it gets read/write dependencies right.
 */
extern struct x8664_pda _proxy_pda;

#define pda_offset(field) offsetof(struct x8664_pda, field)

#define pda_to_op(op,field,val) do {		\
	typedef typeof(_proxy_pda.field) T__;	\
	if (0) { T__ tmp__; tmp__ = (val); }	/* type checking */ \
	switch (sizeof(_proxy_pda.field)) {	\
	case 2:					\
		asm(op "w %1,%%gs:%c2" : 	\
		    "+m" (_proxy_pda.field) :	\
		    "ri" ((T__)val),		\
		    "i"(pda_offset(field))); 	\
 		break;				\
	case 4:					\
		asm(op "l %1,%%gs:%c2" : 	\
		    "+m" (_proxy_pda.field) :	\
		    "ri" ((T__)val),		\
		    "i" (pda_offset(field))); 	\
		break;				\
	case 8:					\
		asm(op "q %1,%%gs:%c2": 	\
		    "+m" (_proxy_pda.field) :	\
		    "ri" ((T__)val),		\
		    "i"(pda_offset(field))); 	\
		break;				\
       default: 				\
		__bad_pda_field();		\
       }					\
       } while (0)

#define pda_from_op(op,field) ({		\
	typeof(_proxy_pda.field) ret__;		\
	switch (sizeof(_proxy_pda.field)) {	\
       	case 2:					\
		asm(op "w %%gs:%c1,%0" : 	\
		    "=r" (ret__) :		\
		    "i" (pda_offset(field)), 	\
		    "m" (_proxy_pda.field)); 	\
		 break;				\
	case 4:					\
		asm(op "l %%gs:%c1,%0":		\
		    "=r" (ret__):		\
		    "i" (pda_offset(field)), 	\
		    "m" (_proxy_pda.field)); 	\
		 break;				\
       case 8:					\
		asm(op "q %%gs:%c1,%0":		\
		    "=r" (ret__) :		\
		    "i" (pda_offset(field)), 	\
		    "m" (_proxy_pda.field)); 	\
		 break;				\
       default: 				\
		__bad_pda_field();		\
       }					\
       ret__; })

#define read_pda(field) pda_from_op("mov",field)
#define write_pda(field,val) pda_to_op("mov",field,val)
#define add_pda(field,val) pda_to_op("add",field,val)
#define sub_pda(field,val) pda_to_op("sub",field,val)
#define or_pda(field,val) pda_to_op("or",field,val)

/* This is not atomic against other CPUs -- CPU preemption needs to be off */
#define test_and_clear_bit_pda(bit,field) ({		\
	int old__;						\
	asm volatile("btr %2,%%gs:%c3\n\tsbbl %0,%0"		\
	    : "=r" (old__), "+m" (_proxy_pda.field) 		\
	    : "dIr" (bit), "i" (pda_offset(field)) : "memory");	\
	old__;							\
})

#endif

#define PDA_STACKOFFSET (5*8)

#endif
