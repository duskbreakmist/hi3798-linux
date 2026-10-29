// SPDX-License-Identifier: GPL-2.0-only
/* MV100 selector-based DFS, using SDK register definitions. Voltage is unchanged.
 * Frequencies are SDK nominal values; higher selectors require explicit opt-in; APLL is unchanged.
 */
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

static struct regmap *crg;
static unsigned int boot_max;
static unsigned int boot_rate;
static unsigned int daily_max_khz = 1000000;
module_param(daily_max_khz, uint, 0444);
MODULE_PARM_DESC(daily_max_khz, "Daily CPU cap: 1000000 recovery or user-requested 1200000 kHz");
static bool allow_overclock;
module_param(allow_overclock, bool, 0444);
MODULE_PARM_DESC(allow_overclock, "Expose experimental 1500 MHz above the 1200 MHz daily cap; voltage is unchanged");
static struct cpufreq_frequency_table mv100_freqs[] = {
    { .driver_data=6, .frequency=400000 },
    { .driver_data=7, .frequency=600000 },
    { .driver_data=1, .frequency=800000 },
    { .driver_data=0, .frequency=1000000 },
    { .driver_data=5, .frequency=1200000 },
    { .driver_data=3, .frequency=1500000 },
    { .frequency=CPUFREQ_TABLE_END },
};
static unsigned int mv100_get(unsigned int cpu)
{
    static const unsigned int rates[]={1000000,800000,0,1500000,24000,1200000,400000,600000};
    unsigned int val;
    if (regmap_read(crg,0x48,&val)) return 0;
    return rates[val&7];
}
static int mv100_target(struct cpufreq_policy *policy,unsigned int index)
{
    unsigned int sel=mv100_freqs[index].driver_data;
    int ret;
    if (mv100_freqs[index].frequency>boot_max) return -EINVAL;
    /* 1500 MHz remains explicitly isolated from automatic governors. */
    if (mv100_freqs[index].frequency > daily_max_khz &&
        (!policy->governor || strcmp(policy->governor->name, "userspace")))
        return -EINVAL;
    /* SDK: select software control, deassert trigger, set mux, trigger. */
    ret=regmap_update_bits(crg,0x48,BIT(9)|BIT(10),BIT(9));
    if (ret) return ret;
    ret=regmap_update_bits(crg,0x48,7,sel);
    if (ret) return ret;
    ret=regmap_update_bits(crg,0x48,BIT(10),BIT(10));
    udelay(10);
    return ret;
}
static int mv100_policy_init(struct cpufreq_policy *policy)
{
    cpumask_copy(policy->cpus,cpu_possible_mask);
    cpufreq_generic_init(policy,mv100_freqs,1000000);
    /* Start at the firmware rate; userspace selects the daily cap later. */
    policy->max=min(boot_rate, policy->cpuinfo.max_freq);
    return 0;
}
static struct cpufreq_driver mv100_driver={
    .name="hi3798mv100",
    .flags=CPUFREQ_NEED_INITIAL_FREQ_CHECK,
    .init=mv100_policy_init,
    .verify=cpufreq_generic_frequency_table_verify,
    .target_index=mv100_target,
    .get=mv100_get,
    .attr=cpufreq_generic_attr,
};
static int __init mv100_cpufreq_init(void)
{
    int i;
    if (!of_machine_is_compatible("hisilicon,hi3798mv100")) return -ENODEV;
    crg=syscon_regmap_lookup_by_compatible("hisilicon,hi3798mv100-crg");
    if (IS_ERR(crg)) return PTR_ERR(crg);
    boot_rate=mv100_get(0);
    if (!boot_rate) return -ENODEV;
    if (daily_max_khz != 1000000 && daily_max_khz != 1200000) return -EINVAL;
    boot_max=allow_overclock ? 1500000U : daily_max_khz;
    for(i=0;mv100_freqs[i].frequency!=CPUFREQ_TABLE_END;i++)
        if(mv100_freqs[i].frequency>boot_max) mv100_freqs[i].frequency=CPUFREQ_ENTRY_INVALID;
    pr_info("MV100 cpufreq: fixed boot voltage, nominal maximum %u kHz\n",boot_max);
    return cpufreq_register_driver(&mv100_driver);
}
device_initcall(mv100_cpufreq_init);
MODULE_LICENSE("GPL");
