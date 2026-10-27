// SPDX-License-Identifier: GPL-2.0-only
/* Hi3798MV100 DT machine. Use normal ioremap and Cortex-A7 cache setup. */
#include <linux/init.h>
#include <linux/printk.h>
#include <asm/mach/arch.h>
#include "hi3798mv100.h"

void hi3798mv100_report_cache(unsigned int cpu)
{
	u32 sctlr, actlr;

	asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r" (sctlr));
	asm volatile("mrc p15, 0, %0, c1, c0, 1" : "=r" (actlr));
	pr_info("MV100 CPU%u: SCTLR=%08x ACTLR=%08x (I=%u C=%u M=%u SMP=%u)\n",
		cpu, sctlr, actlr, (sctlr >> 12) & 1, (sctlr >> 2) & 1,
		sctlr & 1, (actlr >> 6) & 1);
}

static void __init hi3798mv100_init_early(void)
{
	hi3798mv100_report_cache(0);
}

static const char *const hi3798mv100_compat[] __initconst = {
	"hisilicon,hi3798mv100",
	NULL,
};

DT_MACHINE_START(HI3798MV100_DT, "HiSilicon Hi3798MV100 experimental")
	.dt_compat = hi3798mv100_compat,
	.init_early = hi3798mv100_init_early,
MACHINE_END
