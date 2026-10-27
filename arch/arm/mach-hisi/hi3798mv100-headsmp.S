/* SPDX-License-Identifier: GPL-2.0-only */
/* Holding pen based on ARM Ltd.'s Linux SMP entry, Copyright (c) 2003 ARM. */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

ENTRY(hi3798mv100_secondary_startup)
	safe_svcmode_maskall r0
	/* Reset CPU must observe the physical pen before the MMU is enabled. */
	mrc p15, 0, r0, c1, c0, 0
	bic r0, r0, #1
	bic r0, r0, #4
	bic r0, r0, #0x1000
	mcr p15, 0, r0, c1, c0, 0
	isb
	mrc p15, 0, r0, c0, c0, 5
	and r0, r0, #0xff
	adr r4, 1f
	ldmia r4, {r5, r6}
	sub r4, r4, r5
	add r6, r6, r4
2:	ldr r7, [r6]
	cmp r7, r0
	bne 2b
	/* __v7_ca7mp_setup performs L1 invalidation and enables coherency. */
	b secondary_startup
	.align 2
1:	.long .
	.long hi3798mv100_pen_release
ENDPROC(hi3798mv100_secondary_startup)
