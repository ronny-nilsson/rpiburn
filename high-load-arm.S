
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
@ Power consumer for ARM32 with Neon
		.align 2
		.func burn_cpu_neon
		.type burn_cpu_neon, %function
		.global burn_cpu_neon
burn_cpu_neon:
		push		{r4, r5, fp, lr}											@ Prologue
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
		add			fp, sp, #12
		vpush		{q4-q5}

		@ Create a pointer to code ram
		adr			r1, pLabels
		pld			[r1]
		add			r1, r1, #1
		mov			r2, #0

		/* Create a pointer to data ram, which also
		 * happens to be the same location as our C
		 * code global "break out of loop" flag. */
		ldr     	r5, pExit
		sub			r5, r5, #4
		pld			[r5]

		@ Static Neon data for high workload
        vmov.u32	q1, #0
        vmov.u32	q2, #0xffffffff
        vmov.u32	q4, #0xf0f0f0f0
        vmov.u32	q5, #0x0f0f0f0f

		/* Tight loop where we alternate reading
		 * unaligned data from both code and data
		 * ram, combined with Neon calculations. */
		b		1f
		.align 7
1:		ldr			r3, [r5, #1]												@ Poll do_exit, time to exit loop?
		vabd.u32	q0, q1, q2
		ldr			r0, [r1, r2, lsl #2]!
		vaba.u32	q3, q4, q5
		movs		r2, r3
		beq			1b

		movne		r0, #0														@ EXIT_SUCCESS
		moveq		r0, #1														@ EXIT_FAILURE
		vpop		{q4-q5}
#else
		mov			r0, #1														@ EXIT_FAILURE
#endif
		pop			{r4, r5, fp, pc}											@ Epilogue
		.endfunc



@-------------------------------------------------------------
@ Power consumer for ARM32
		.align	2
		.func	burn_cpu_arm
		.type	burn_cpu_arm, %function
		.global	burn_cpu_arm
burn_cpu_arm:
		push	{r4, r5, r6, r7, fp, lr}										@ Prologue
		add		fp, sp, #12

		@ Create a pointer to code ram
		adr		r1, pLabels
		pld		[r1]

		/* Create a pointer to data ram, which also
		 * happens to be the same location as our C
		 * code global "break out of loop" flag. */
		ldr		r5, pExit
 		sub		r5, r5, #4
		pld		[r5]

		/* Tight low latency optimized loop where
		 * we alternate reading unaligned data from
		 * both code and data ram. Two instructions
		 * per cycle by Cortex-A7 and one by ARM11.
		 * Code alignment has impact. */
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
@ Name to address mapping for global C variables and
@ cache line aligned code ram dummy data.
		.align  7
pLabels:.word   0
pExit:	.word   do_exit
		.align  5
		.word	0


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
do_exit:.word 0


