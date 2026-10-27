// SPDX-License-Identifier: GPL-2.0-only
/*
 * Register offsets and reset ordering checked against HiSilicon's 4.4 BSP.
 * Holding-pen handshake follows the existing ARM Linux SMP implementation.
 * CPU hotplug is deliberately not implemented in this initial port.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/of_address.h>
#include <linux/smp.h>
#include <linux/sizes.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include "hi3798mv100.h"

#define MV100_BOOT_VECTOR 0xffff0000
#define MV100_CPU_LP 0x48
#define MV100_CPU_RESET 0x50
#define MV100_CPU_SW_BEGIN BIT(10)

int hi3798mv100_pen_release = -1;
static void __iomem *mv100_crg;
static DEFINE_RAW_SPINLOCK(mv100_boot_lock);

static void mv100_write_pen(int value)
{
	WRITE_ONCE(hi3798mv100_pen_release, value);
	/* Publish the pen before releasing reset; the waiting CPU has caches off. */
	smp_wmb();
	sync_cache_w(&hi3798mv100_pen_release);
}

static void __init mv100_smp_prepare(unsigned int max_cpus)
{
	struct device_node *node;
	void __iomem *vector;

	if (max_cpus < 2)
		return;
	node = of_find_compatible_node(NULL, NULL, "hisilicon,hi3798mv100-crg");
	if (!node)
		return;
	mv100_crg = of_iomap(node, 0);
	of_node_put(node);
	if (!mv100_crg)
		return;
	vector = ioremap(MV100_BOOT_VECTOR, SZ_4K);
	if (!vector) {
		iounmap(mv100_crg);
		mv100_crg = NULL;
		return;
	}
	/* Reset ROM trampoline: ldr pc, [pc, #-4], followed by entry address. */
	writel(0xe51ff004, vector);
	writel(__pa_symbol(hi3798mv100_secondary_startup), vector + 4);
	/* Order both trampoline writes before a secondary can leave reset. */
	dsb(sy);
	iounmap(vector);
}

static void mv100_secondary_init(unsigned int cpu)
{
	hi3798mv100_report_cache(cpu);
	mv100_write_pen(-1);
	raw_spin_lock(&mv100_boot_lock);
	raw_spin_unlock(&mv100_boot_lock);
}

static int mv100_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned int hwid = cpu_logical_map(cpu) & 0xff;
	unsigned long timeout;
	u32 saved, value;

	if (!mv100_crg)
		return -ENODEV;
	if (!hwid || hwid > 3)
		return -EINVAL;
	raw_spin_lock(&mv100_boot_lock);
	mv100_write_pen(hwid);
	/* BSP requires switching to 400 MHz while releasing slave reset. */
	saved = readl(mv100_crg + MV100_CPU_LP);
	writel(0x306, mv100_crg + MV100_CPU_LP);
	writel(0x706, mv100_crg + MV100_CPU_LP);
	value = readl(mv100_crg + MV100_CPU_RESET);
	value &= ~BIT(hwid + 12);
	writel(value, mv100_crg + MV100_CPU_RESET);
	value &= ~BIT(hwid + 20);
	writel(value, mv100_crg + MV100_CPU_RESET);
	value &= ~BIT(hwid + 16);
	writel(value, mv100_crg + MV100_CPU_RESET);
	writel(saved & ~MV100_CPU_SW_BEGIN, mv100_crg + MV100_CPU_LP);
	writel(saved, mv100_crg + MV100_CPU_LP);
	readl(mv100_crg + MV100_CPU_LP);
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));
	timeout = jiffies + 5 * HZ;
	while (time_before(jiffies, timeout)) {
		if (READ_ONCE(hi3798mv100_pen_release) == -1)
			break;
		udelay(10);
	}
	raw_spin_unlock(&mv100_boot_lock);
	return READ_ONCE(hi3798mv100_pen_release) == -1 ? 0 : -ETIMEDOUT;
}

static const struct smp_operations mv100_smp_ops __initconst = {
	.smp_prepare_cpus = mv100_smp_prepare,
	.smp_secondary_init = mv100_secondary_init,
	.smp_boot_secondary = mv100_boot_secondary,
};
CPU_METHOD_OF_DECLARE(mv100_smp, "hisilicon,hi3798mv100-smp", &mv100_smp_ops);
