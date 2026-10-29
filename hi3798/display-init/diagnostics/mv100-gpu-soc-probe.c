// SPDX-License-Identifier: GPL-2.0-only
/* One read-only snapshot of always-on controls, not the GPU aperture. */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>

static int __init mv100_gpu_soc_probe_init(void)
{
	void __iomem *crg, *pmc;

	if (!of_machine_is_compatible("hisilicon,hi3798mv100"))
		return -ENODEV;
	crg = ioremap(0xf8a22000, 0x200);
	if (!crg)
		return -ENOMEM;
	pmc = ioremap(0xf8a23000, 0x200);
	if (!pmc) {
		iounmap(crg);
		return -ENOMEM;
	}
	pr_info("mv100_gpu_soc_probe: CRG48=%08x CRGd4=%08x CRG124=%08x CRG154=%08x\n",
		readl(crg + 0x48), readl(crg + 0xd4),
		readl(crg + 0x124), readl(crg + 0x154));
	pr_info("mv100_gpu_soc_probe: PMC48=%08x PMC54=%08x PMCb0=%08x\n",
		readl(pmc + 0x48), readl(pmc + 0x54), readl(pmc + 0xb0));
	iounmap(pmc);
	iounmap(crg);
	return 0;
}

static void __exit mv100_gpu_soc_probe_exit(void) { }
module_init(mv100_gpu_soc_probe_init);
module_exit(mv100_gpu_soc_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only Hi3798MV100 GPU SoC controls snapshot");
