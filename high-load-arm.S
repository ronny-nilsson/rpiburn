
/* This file is in large inspired by cpuburn-a7 by
 * https://github.com/ssvb/cpuburn-arm
 * Copyright © 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
 *
 * Additions for Raspberry Pi by
 * Nard Linux SDK
 * http://www.arbetsmyra.dyndns.org/nard
 * Copyright (C) 2014-2017 Ronny Nilsson
 */



		.syntax divided
		.section .text
		.arm


@-------------------------------------------------------------
@ Power consumer for ARM Cortex-A7
		.align 7
		.func burn_cpu_a53
		.type burn_cpu_a53, %function
		.global burn_cpu_a53
burn_cpu_a53:
		push	{r4, r5, fp, lr}												@ Prologue
		add		fp, sp, #12

		@ Create a pointer to code ram
		adr		r1, burn_cpu_a7

		/* Create a pointer to data ram, which also
		 * happens to be the same location as our C
		 * code global "break out of loop" flag. */
		ldr     r5, name2adr
 		sub		r5, r5, #4
		ldr		r3, [r5, #1]

		/* Tight loop where we alternate reading
		 * unaligned data from both code and data
		 * ram. 2 instructions per cycle. Code
		 * alignment has impact. */
		b		1f
		.align 7
1:		movs	r2, r3
		ldr		r3, [r5, #1]													@ Poll do_exit, time to exit loop?
		ldr		r0, [r1, #1]
		beq		1b

		movne	r0, #0
		moveq	r0, #1
		pop		{r4, r5, fp, pc}												@ Epilogue
		.endfunc


@-------------------------------------------------------------
@ Power consumer for ARM Cortex-A7
		.align	7
		.func	burn_cpu_a7
		.type	burn_cpu_a7, %function
		.global	burn_cpu_a7
burn_cpu_a7:
		push	{r4, r5, r6, r7, fp, lr}										@ Prologue
		add		fp, sp, #12

		@ Create a pointer to code ram
		adr		r1, burn_cpu_a7
		pld		[r1]

		/* Create a pointer to data ram, which also
		 * happens to be the same location as our C
		 * code global "break out of loop" flag. */
		ldr     r5, name2adr
 		sub		r5, r5, #4
		pld		[r5]

		/* Tight loop where we alternate reading
		 * unaligned data from both code and data
		 * ram. Two instructions per cycle. Code
		 * alignment has impact. */
		mov		r3, #0
		b		1f
		.align	7
1:		ldr		r0, [r1, #1]
		movs	r2, r3
		ldr		r3, [r5, #1]													@ Poll do_exit, time to exit loop?
		mov		r4, r1
		ldr		r6, [r1, #1]
		mov		r2, r1
		ldr		r7, [r5, #1]													@ Poll do_exit, time to exit loop?
		beq		1b

		movne	r0, #0															@ EXIT_SUCCESS
		moveq	r0, #1															@ EXIT_FAILURE
		pop		{r4, r5, r6, r7, fp, pc}										@ Epilogue
		.endfunc


@-------------------------------------------------------------
@ Name to address mapping for global C variables
        .align  2
name2adr:
        .word   do_exit




@-------------------------------------------------------------
		.section .bss
        .align  7

		/* Tell the compiler <do_exit> is one byte
		 * (as in C src) but reserve eight bytes as
		 * guard for asm dynamic unaligned access. */
		.type do_exit, %object
		.size do_exit, 1
		.global do_exit
		.word 0
do_exit:
		.word 0


