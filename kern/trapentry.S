/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>
#include <kern/macro.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushq $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushq $0;							\
	pushq $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
   Find the PUSHA,POPA macros in kern/macro.h. Since amd64 doesn't support
   pusha,popa so you will find these macros handy.
 */

	TRAPHANDLER_NOEC(trap_divide, T_DIVIDE);
	TRAPHANDLER_NOEC(trap_debug, T_DEBUG);
	TRAPHANDLER_NOEC(trap_nmi, T_NMI);
	TRAPHANDLER_NOEC(trap_bpoint, T_BRKPT);
	TRAPHANDLER_NOEC(trap_oflow, T_OFLOW);
	TRAPHANDLER_NOEC(trap_brexc, T_BOUND);
	TRAPHANDLER_NOEC(trap_invop, T_ILLOP);
	TRAPHANDLER_NOEC(trap_dna, T_DEVICE);
	TRAPHANDLER(trap_dfault, T_DBLFLT); 
	TRAPHANDLER(trap_cpoverrun, T_COPROC);
	TRAPHANDLER(trap_invtss, T_TSS);
	TRAPHANDLER(trap_segnp, T_SEGNP);
	TRAPHANDLER(trap_sfault, T_STACK);
	TRAPHANDLER(trap_gpfault, T_GPFLT);
	TRAPHANDLER(trap_pfault, T_PGFLT);
	//TRAPHANDLER(trap_utrap, T_UTRAP);
	TRAPHANDLER_NOEC(trap_fpuerr, T_FPERR);
	TRAPHANDLER_NOEC(trap_alignchk, T_ALIGN);
	TRAPHANDLER_NOEC(trap_mchk, T_MCHK);
	TRAPHANDLER_NOEC(trap_simdexc, T_SIMDERR);
	
	TRAPHANDLER_NOEC(trap_syscall, T_SYSCALL);

	TRAPHANDLER_NOEC(trap_IRQ0, T_IRQ0);
	TRAPHANDLER_NOEC(trap_IRQ1, T_IRQ1);
	TRAPHANDLER_NOEC(trap_IRQ2, T_IRQ2);
	TRAPHANDLER_NOEC(trap_IRQ3, T_IRQ3);
	TRAPHANDLER_NOEC(trap_IRQ4, T_IRQ4);
	TRAPHANDLER_NOEC(trap_IRQ5, T_IRQ5);
	TRAPHANDLER_NOEC(trap_IRQ6, T_IRQ6);
	TRAPHANDLER_NOEC(trap_IRQ7, T_IRQ7);
	TRAPHANDLER_NOEC(trap_IRQ8, T_IRQ8);
	TRAPHANDLER_NOEC(trap_IRQ9, T_IRQ9);
	TRAPHANDLER_NOEC(trap_IRQ10, T_IRQ10);
	TRAPHANDLER_NOEC(trap_IRQ11, T_IRQ11);
	TRAPHANDLER_NOEC(trap_IRQ12, T_IRQ12);
	TRAPHANDLER_NOEC(trap_IRQ13, T_IRQ13);
	TRAPHANDLER_NOEC(trap_IRQ14, T_IRQ14);
	TRAPHANDLER_NOEC(trap_IRQ15, T_IRQ15);
/*
 * Lab 3: Your code here for _alltraps
 *
 * Hint: Be sure to review the x64 calling convention from lab1
 *       for how to pass a pointer to the trapframe.
 */
 
 _alltraps:
/* 	
	Registers will be corrupted when you do this
	Can't even use stack since trap frame will be disturbed
	movq %ds, %rsi
 	pushq %rsi
 	movq %es, %rsi
 	pushq %rsi
 */
 	subq $16, %rsp
 	movw $0, 14(%rsp)
 	movw $0, 12(%rsp)
 	movw $0, 10(%rsp)
 	movw %ds, 8(%rsp)
 	movw $0, 6(%rsp)
 	movw $0, 4(%rsp)
 	movw $0, 2(%rsp)
 	movw %es, (%rsp)
 	PUSHA
 	movq $GD_KD, %rax
 	movq %rax, %ds
 	movq %rax, %es
 	movq %rsp, %rdi
 	call trap
 	POPA_
 	popq %rdx
 	movq %rdx, %es
 	popq %rdx
 	movq %rdx, %ds
 	iretq
 	
 	
 	

