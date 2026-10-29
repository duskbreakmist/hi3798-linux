// SPDX-License-Identifier: GPL-2.0-only
/* MV100 GPU DFS: existing PLL taps, SDK PMC handshake, no voltage/PLL writes. */
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>

struct clk *hi3798mv100_gpu_clk_register(struct device *dev, void __iomem *crg);

struct mv100_gpu_clock {
	struct clk_hw hw;
	void __iomem *crg;
	void __iomem *pmc;
	struct device *dev;
};

static const unsigned long gpu_rates[] = {
	432000000, 400000000, 375000000, 345600000,
	300000000, 250000000, 200000000, 500000000,
};

static unsigned long mv100_gpu_recalc(struct clk_hw *hw, unsigned long parent)
{
	struct mv100_gpu_clock *gpu = container_of(hw, struct mv100_gpu_clock, hw);

	return gpu_rates[(readl(gpu->crg + 0x154) >> 5) & 7];
}

static long mv100_gpu_round(struct clk_hw *hw, unsigned long rate,
			     unsigned long *parent)
{
	unsigned long best = 200000000, delta = ULONG_MAX;
	unsigned int i;

	/* Only the 200..400 MHz operating range is offered for daily use. */
	for (i = 1; i <= 6; i++) {
		unsigned long d = rate > gpu_rates[i] ? rate - gpu_rates[i] : gpu_rates[i] - rate;

		if (d < delta) {
			delta = d;
			best = gpu_rates[i];
		}
	}
	return best;
}

static int mv100_gpu_set(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent)
{
	struct mv100_gpu_clock *gpu = container_of(hw, struct mv100_gpu_clock, hw);
	u32 old_mux, old_freq, old_ctrl, old_time, freq, value;
	unsigned int sel;
	int ret;

	for (sel = 1; sel <= 6; sel++)
		if (gpu_rates[sel] == rate)
			break;
	if (sel > 6)
		return -EINVAL;
	if (mv100_gpu_recalc(hw, parent) == rate)
		return 0;

	old_mux = readl(gpu->crg + 0x124);
	old_freq = readl(gpu->pmc + 0x54);
	old_ctrl = readl(gpu->pmc + 0xb0);
	old_time = readl(gpu->pmc + 0x48);
	/* SDK hisi_pmc_setparameter: Mali PMU/counting, hardware power-good/FSM. */
	writel(old_ctrl & ~3U, gpu->pmc + 0xb0);
	writel(0x00ffff88, gpu->pmc + 0x48);
	freq = (old_freq & ~(GENMASK(15, 0) | BIT(16) | BIT(18) | BIT(21))) |
		BIT(20) | 0x2fff;
	if (rate > mv100_gpu_recalc(hw, parent))
		freq |= BIT(18);
	writel(freq, gpu->pmc + 0x54);
	writel((old_mux & ~(BIT(9) | BIT(10) | 7U)) | sel, gpu->crg + 0x124);
	writel(freq | BIT(21), gpu->pmc + 0x54);
	readl(gpu->pmc + 0x54);
	writel(freq, gpu->pmc + 0x54);
	ret = readl_poll_timeout(gpu->crg + 0x154, value,
		(value & BIT(4)) && ((value >> 5) & 7) == sel, 10, 20000);
	if (!ret)
		return 0;

	dev_err(gpu->dev, "GPU DFS %lu Hz did not latch: status=%08x; restoring\n", rate, value);
	writel(old_mux, gpu->crg + 0x124);
	writel(old_freq | BIT(21), gpu->pmc + 0x54);
	readl(gpu->pmc + 0x54);
	writel(old_freq & ~BIT(21), gpu->pmc + 0x54);
	writel(old_ctrl, gpu->pmc + 0xb0);
	writel(old_time, gpu->pmc + 0x48);
	return ret;
}

static const struct clk_ops mv100_gpu_ops = {
	.recalc_rate = mv100_gpu_recalc,
	.round_rate = mv100_gpu_round,
	.set_rate = mv100_gpu_set,
};

struct clk *hi3798mv100_gpu_clk_register(struct device *dev, void __iomem *crg)
{
	struct mv100_gpu_clock *gpu;
	struct clk_init_data init = {
		.name = "gpu_dfs",
		.ops = &mv100_gpu_ops,
		.flags = CLK_GET_RATE_NOCACHE,
	};
	int ret;

	gpu = devm_kzalloc(dev, sizeof(*gpu), GFP_KERNEL);
	if (!gpu)
		return ERR_PTR(-ENOMEM);
	gpu->pmc = devm_ioremap(dev, 0xf8a23000, 0x200);
	if (!gpu->pmc)
		return ERR_PTR(-ENOMEM);
	gpu->crg = crg;
	gpu->dev = dev;
	gpu->hw.init = &init;
	ret = devm_clk_hw_register(dev, &gpu->hw);
	if (ret)
		return ERR_PTR(ret);
	return gpu->hw.clk;
}
