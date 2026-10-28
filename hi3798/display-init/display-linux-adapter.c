// SPDX-License-Identifier: GPL-2.0-only
/* Experimental init bridge, not a complete DRM/KMS driver. */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/slab.h>

extern int HI_UNF_DISP_Init(void);
extern int HI_UNF_DISP_Open(int display);
extern int HI_UNF_DISP_Close(int display);
extern int HI_UNF_DISP_DeInit(void);
extern int mv100_sdk_hdmi_start(void);
extern void mv100_sdk_gfx_start(unsigned int address);
extern void mv100_sdk_gfx_dump(void);
void *mv100_mmio_address(unsigned long address);
void mv100_sdk_udelay(unsigned long us);
void mv100_sdk_mdelay(unsigned long ms);
int mv100_sdk_printf(const char *fmt, ...);
void HI_LogOut(unsigned int level, int module, unsigned char *function,
               unsigned int line, const char *fmt, ...);

void *g_pstRegCrg;
static bool enable = true;
static bool direct_pattern;
module_param(enable, bool, 0444);
module_param(direct_pattern, bool, 0444);
MODULE_PARM_DESC(enable, "Explicitly enable experimental HDMI background initialization");
static bool opened;
static bool gfx_active;
static int display_status(char *buffer, const struct kernel_param *parameter)
{
    void __iomem *fb;
    u32 pixel = 0;
    if (!opened)
        return scnprintf(buffer, PAGE_SIZE, "inactive\n");
    mv100_sdk_gfx_dump();
    if (gfx_active) {
        fb = ioremap_wc(0x3e000000, 4096);
        if (fb) {
            pixel = readl(fb);
            iounmap(fb);
        }
    }
    return scnprintf(buffer, PAGE_SIZE, "gfx=%u first_pixel=%08x\n", gfx_active, pixel);
}
static const struct kernel_param_ops status_ops = {.get = display_status};
module_param_cb(status, &status_ops, NULL, 0444);
static struct {
    phys_addr_t phys;
    size_t size;
    void __iomem *virt;
} regions[] = {
    {0xf8000000, 0x1000},
    {0xf8a20000, 0x4000},
    {0xf8a90000, 0x1000},
    {0xf8cc0000, 0x10000},
    {0xf8cd0000, 0x1000},
    {0xf8ce0000, 0x2000},
};

void *mv100_mmio_address(unsigned long address)
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(regions); ++i)
        if (address >= regions[i].phys && address < regions[i].phys + regions[i].size)
            return (__force void *)(regions[i].virt + address - regions[i].phys);
    panic("MV100 display: unexpected MMIO address %08lx", address);
}

void mv100_sdk_udelay(unsigned long us)
{
    while (us > 1000) {
        udelay(1000);
        us -= 1000;
    }
    udelay(us);
}

void mv100_sdk_mdelay(unsigned long ms)
{
    while (ms--)
        mv100_sdk_udelay(1000);
}

int mv100_sdk_printf(const char *fmt, ...)
{
    va_list args;
    struct va_format vaf;
    int result;
    va_start(args, fmt);
    vaf.fmt = fmt;
    vaf.va = &args;
    result = printk(KERN_INFO "mv100-display: %pV", &vaf);
    va_end(args);
    return result;
}

void HI_LogOut(unsigned int level, int module, unsigned char *function,
               unsigned int line, const char *fmt, ...)
{
    va_list args;
    struct va_format vaf;
    va_start(args, fmt);
    vaf.fmt = fmt;
    vaf.va = &args;
    printk(KERN_INFO "mv100-display %s:%u: %pV", function, line, &vaf);
    va_end(args);
}

static void release_regions(void)
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(regions); ++i)
        if (regions[i].virt) {
            iounmap(regions[i].virt);
            regions[i].virt = NULL;
        }
}

static bool reserved_framebuffer_present(void)
{
    struct device_node *node;
    const __be32 *reg;
    int length;
    bool valid;
    node = of_find_node_by_path("/reserved-memory/framebuffer@3e000000");
    reg = node ? of_get_property(node, "reg", &length) : NULL;
    valid = reg && length == 8 && be32_to_cpup(reg) == 0x3e000000 &&
        be32_to_cpup(reg + 1) == 0x800000 && of_property_read_bool(node, "no-map");
    of_node_put(node);
    return valid;
}

static int __init display_init(void)
{
    unsigned int i;
    int ret;
    if (!of_machine_is_compatible("hisilicon,hi3798mv100"))
        return -ENODEV;
    if (!reserved_framebuffer_present()) {
        pr_info("MV100 display init: no reserved display buffer; skipping\n");
        return 0;
    }
    if (!enable) {
        pr_info("MV100 display init bridge: inactive; enable=1 required\n");
        return 0;
    }
    for (i = 0; i < ARRAY_SIZE(regions); ++i) {
        regions[i].virt = ioremap(regions[i].phys, regions[i].size);
        if (!regions[i].virt) {
            release_regions();
            return -ENOMEM;
        }
    }
    g_pstRegCrg = mv100_mmio_address(0xf8a22000);
    ret = HI_UNF_DISP_Init();
    pr_info("MV100 display init returned %d\n", ret);
    if (!ret) {
        ret = HI_UNF_DISP_Open(1);
        pr_info("MV100 display open HD returned %d\n", ret);
        opened = !ret;
        if (!ret) {
            ret = mv100_sdk_hdmi_start();
            pr_info("MV100 explicit HDMI init/open returned %d\n", ret);
        }
    }
    if (ret) {
        if (opened)
            HI_UNF_DISP_Close(1);
        HI_UNF_DISP_DeInit();
        opened = false;
        gfx_active = false;
        release_regions();
    } else {
        struct device_node *node;
        const __be32 *reg;
        int length;
        node = of_find_node_by_path("/reserved-memory/framebuffer@3e000000");
        reg = node ? of_get_property(node, "reg", &length) : NULL;
        if (reg && length == 8 &&
            be32_to_cpup(reg) == 0x3e000000 && be32_to_cpup(reg + 1) == 0x800000 &&
            of_property_read_bool(node, "no-map")) {
            mv100_sdk_gfx_start(0x3e000000);
            gfx_active = true;
            if (direct_pattern) {
                static const u32 colors[] = {0xffffffff, 0xffffff00, 0xff00ffff,
                    0xff00ff00, 0xffff00ff, 0xffff0000, 0xff0000ff, 0xff000000};
                void __iomem *fb = ioremap_wc(0x3e000000, 0x800000);
                unsigned int x, y;
                if (fb) {
                    for (y = 0; y < 1080; ++y)
                        for (x = 0; x < 1920; ++x)
                            writel_relaxed(colors[x / 240], fb + (y * 1920 + x) * 4);
                    wmb();
                    pr_info("MV100 direct framebuffer pattern: first pixel %08x\n", readl(fb));
                    iounmap(fb);
                }
            }
            pr_info("MV100 G0 scanout: reserved framebuffer 3e000000 1920x1080 stride 7680\n");
        }
        of_node_put(node);
    }
    return ret;
}

static void __exit display_exit(void)
{
    if (opened) {
        HI_UNF_DISP_Close(1);
        HI_UNF_DISP_DeInit();
        release_regions();
    }
}
module_init(display_init);
module_exit(display_exit);
/* The linked SDK code retains its original licensing. */
MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION("MV100 experimental display initialization bridge");
