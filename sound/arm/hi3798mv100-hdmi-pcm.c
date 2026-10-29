// SPDX-License-Identifier: GPL-2.0-only
/* MV100/S40 HDMI PCM: native ALSA ring, AIAO TX3, no vendor AO/DSP ABI. */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#define TX 0x2300
#define TX_CTRL (TX + 4)
#define TX_ADDR (TX + 0x80)
#define TX_SIZE (TX + 0x84)
#define TX_WPTR (TX + 0x88)
#define TX_RPTR (TX + 0x8c)
#define TX_ENABLE BIT(28)
#define CRG70 0x118

struct mv100_pcm {
	void __iomem *aiao, *crg, *hdmi;
	struct snd_pcm_substream *stream;
	struct hrtimer timer;
	bool running;
	bool full_pending;
	u32 pending_wptr;
	spinlock_t lock;
	u32 last_pos, period_acc;
};

static u8 hdmi_read(struct mv100_pcm *p, u8 reg)
{
	return readl(p->hdmi + reg * 4) & 0xff;
}

static void hdmi_write(struct mv100_pcm *p, u8 reg, u8 val)
{
	writel(val, p->hdmi + reg * 4);
}

static void mv100_hdmi_config(struct mv100_pcm *p)
{
	/* SiI transmitter P1; SDK si_audio.c / si_defstx.h, 48k stereo LPCM. */
	hdmi_write(p, 0x14, 0x10); /* SD0, audio off during FIFO setup */
	hdmi_write(p, 0x02, 1); /* MCLK = 256 Fs */
	hdmi_write(p, 0x03, 0); /* N = 6144, little endian */
	hdmi_write(p, 0x04, 0x18);
	hdmi_write(p, 0x05, 0);
	hdmi_write(p, 0x1d, 0); /* I2S, uncompressed */
	hdmi_write(p, 0x1e, 0);
	hdmi_write(p, 0x1f, 0);
	hdmi_write(p, 0x20, 0);
	hdmi_write(p, 0x21, 2); /* IEC60958 48 kHz */
	hdmi_write(p, 0x22, 0x22); /* 16-bit samples */
	hdmi_write(p, 0x23, 0);
	hdmi_write(p, 0x24, 2);
	hdmi_write(p, 0x2f, (hdmi_read(p, 0x2f) & ~2) | 1);
	/* Audio InfoFrame: type/version/length/checksum + 10 payload bytes. */
	hdmi_write(p, 0x3e, hdmi_read(p, 0x3e) & ~0x30);
	hdmi_write(p, 0x80, 0x84);
	hdmi_write(p, 0x81, 1);
	hdmi_write(p, 0x82, 10);
	hdmi_write(p, 0x83, 0x70); /* -(0x84 + 1 + 10 + 1) */
	hdmi_write(p, 0x84, 1); /* two channels, format from stream */
	for (int i = 0x85; i <= 0x8d; i++)
		hdmi_write(p, i, 0);
	hdmi_write(p, 0x3e, hdmi_read(p, 0x3e) | 0x30);
	hdmi_write(p, 0x01, hdmi_read(p, 0x01) | 6); /* N/CTS + internal MCLK */
	msleep(1);
	hdmi_write(p, 0x14, 0x11);
}

static const struct snd_pcm_hardware mv100_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000, .rate_max = 48000,
	.channels_min = 2, .channels_max = 2,
	.buffer_bytes_max = 65536, .period_bytes_min = 512,
	.period_bytes_max = 8192, .periods_min = 2, .periods_max = 64,
};

static enum hrtimer_restart mv100_tick(struct hrtimer *timer)
{
	struct mv100_pcm *p = container_of(timer, struct mv100_pcm, timer);
	struct snd_pcm_substream *s = READ_ONCE(p->stream);
	u32 pos, size, delta, period;
	unsigned long flags;
	if (!READ_ONCE(p->running) || !s)
		return HRTIMER_NORESTART;
	size = snd_pcm_lib_buffer_bytes(s);
	period = snd_pcm_lib_period_bytes(s);
	pos = readl(p->aiao + TX_RPTR) % size;
	spin_lock_irqsave(&p->lock, flags);
	if (p->full_pending && pos != p->pending_wptr) {
		dma_wmb();
		writel(p->pending_wptr, p->aiao + TX_WPTR);
		p->full_pending = false;
	}
	spin_unlock_irqrestore(&p->lock, flags);
	delta = (pos + size - p->last_pos) % size;
	p->last_pos = pos;
	p->period_acc += delta;
	while (p->period_acc >= period) {
		p->period_acc -= period;
		snd_pcm_period_elapsed(s);
		if (!READ_ONCE(p->running))
			return HRTIMER_NORESTART;
	}
	hrtimer_forward_now(timer, ms_to_ktime(2));
	return HRTIMER_RESTART;
}

static int mv100_open(struct snd_pcm_substream *s)
{
	struct mv100_pcm *p = snd_pcm_substream_chip(s);
	s->runtime->hw = mv100_hw;
	p->stream = s;
	return snd_pcm_hw_constraint_step(s->runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 128);
}

static int mv100_sync_stop(struct snd_pcm_substream *s)
{
	struct mv100_pcm *p = snd_pcm_substream_chip(s);
	hrtimer_cancel(&p->timer);
	return 0;
}

static int mv100_close(struct snd_pcm_substream *s)
{
	struct mv100_pcm *p = snd_pcm_substream_chip(s);
	WRITE_ONCE(p->running, false);
	hrtimer_cancel(&p->timer);
	writel(0x7900, p->aiao + TX_CTRL);
	p->stream = NULL;
	return 0;
}

static int mv100_prepare(struct snd_pcm_substream *s)
{
	struct mv100_pcm *p = snd_pcm_substream_chip(s);
	struct snd_pcm_runtime *r = s->runtime;
	u32 value;
	WRITE_ONCE(p->running, false);
	hrtimer_cancel(&p->timer);
	writel(0x7900, p->aiao + TX_CTRL);
	/* SDK 491.52-MHz PLL table: MCLK=256*48k, BCLK divider4, FS divider64. */
	writel(0x333333, p->aiao + 0x158);
	writel(0x133, p->aiao + 0x15c);
	value = readl(p->aiao + 0x2c);
	writel((value & ~(0xf << 12)) | (11 << 12), p->aiao + 0x2c);
	/* TX3 DMA source3; SD0..3 direct; I2S, stereo, 16 bits. */
	writel(0xe4300014, p->aiao + TX);
	writel(lower_32_bits(r->dma_addr), p->aiao + TX_ADDR);
	writel(snd_pcm_lib_buffer_bytes(s), p->aiao + TX_SIZE);
	writel(0, p->aiao + TX_RPTR);
	writel(0, p->aiao + TX_WPTR);
	writel(snd_pcm_lib_period_bytes(s), p->aiao + TX + 0x90);
	writel(snd_pcm_lib_period_bytes(s), p->aiao + TX + 0x94);
	writel(0, p->aiao + TX + 0xa0); /* no TX IRQ until separately validated */
	writel(0xff, p->aiao + TX + 0xac);
	p->last_pos = p->period_acc = 0;
	p->full_pending = false;
	mv100_hdmi_config(p);
	return 0;
}

static int mv100_ack(struct snd_pcm_substream *s)
{
	struct mv100_pcm *p = snd_pcm_substream_chip(s);
	snd_pcm_uframes_t frames = READ_ONCE(s->runtime->control->appl_ptr);
	u32 size = snd_pcm_lib_buffer_bytes(s);
	u32 wp = frames_to_bytes(s->runtime, frames % s->runtime->buffer_size);
	snd_pcm_sframes_t queued = frames - READ_ONCE(s->runtime->status->hw_ptr);
	unsigned long flags;
	if (queued < 0)
		queued += s->runtime->boundary;
	spin_lock_irqsave(&p->lock, flags);
	p->full_pending = false;
	/* Equal hardware pointers mean empty. Release the final 128 bytes
	 * after DMA advances when ALSA initially pre-fills the entire ring. */
	if (queued >= s->runtime->buffer_size) {
		p->pending_wptr = wp;
		wp = (wp + size - 128) % size;
		p->full_pending = true;
	}
	dma_wmb();
	writel(wp, p->aiao + TX_WPTR);
	spin_unlock_irqrestore(&p->lock, flags);
	return 0;
}

static int mv100_trigger(struct snd_pcm_substream *s, int cmd)
{
	struct mv100_pcm *p = snd_pcm_substream_chip(s);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		mv100_ack(s);
		WRITE_ONCE(p->running, true);
		writel(0x7900 | TX_ENABLE, p->aiao + TX_CTRL);
		hrtimer_start(&p->timer, ms_to_ktime(2), HRTIMER_MODE_REL);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		WRITE_ONCE(p->running, false);
		writel(0x7900, p->aiao + TX_CTRL);
		return 0;
	default: return -EINVAL;
	}
}

static snd_pcm_uframes_t mv100_pointer(struct snd_pcm_substream *s)
{
	struct mv100_pcm *p = snd_pcm_substream_chip(s);
	return bytes_to_frames(s->runtime, readl(p->aiao + TX_RPTR) % snd_pcm_lib_buffer_bytes(s));
}

static const struct snd_pcm_ops mv100_ops = {
	.open = mv100_open, .close = mv100_close, .ioctl = snd_pcm_lib_ioctl,
	.prepare = mv100_prepare, .trigger = mv100_trigger,
	.pointer = mv100_pointer, .ack = mv100_ack, .sync_stop = mv100_sync_stop,
};

static void mv100_cancel(void *data)
{
	struct mv100_pcm *p = data;
	WRITE_ONCE(p->running, false);
	hrtimer_cancel(&p->timer);
	writel(0x7900, p->aiao + TX_CTRL);
}

static ssize_t dma_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mv100_pcm *p = dev_get_drvdata(dev);
	return sysfs_emit(buf, "running=%u ctrl=%08x wptr=%u rptr=%u ws=%u bclk=%u\n",
		READ_ONCE(p->running), readl(p->aiao + TX_CTRL),
		readl(p->aiao + TX_WPTR), readl(p->aiao + TX_RPTR),
		readl(p->aiao + TX + 0x20), readl(p->aiao + TX + 0x24));
}
static DEVICE_ATTR_RO(dma_status);
static struct attribute *mv100_attrs[] = { &dev_attr_dma_status.attr, NULL };
static const struct attribute_group mv100_group = { .attrs = mv100_attrs };

static int mv100_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct mv100_pcm *p;
	u32 value;
	int ret;
	ret = snd_devm_card_new(&pdev->dev, -1, "MV100HDMI", THIS_MODULE, sizeof(*p), &card);
	if (ret < 0) return ret;
	p = card->private_data;
	platform_set_drvdata(pdev, p);
	spin_lock_init(&p->lock);
	p->aiao = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(p->aiao)) return PTR_ERR(p->aiao);
	p->crg = devm_ioremap(&pdev->dev, 0xf8a22000, 0x1000);
	p->hdmi = devm_ioremap(&pdev->dev, 0xf8ce0400, 0x400);
	if (!p->crg || !p->hdmi) return -ENOMEM;
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) return ret;
	/* SDK CRG70 sequence; do not touch CPU/GPU/MMC clock roots. */
	value = readl(p->crg + CRG70);
	writel(value | BIT(4), p->crg + CRG70);
	writel(value | BIT(4) | BIT(0), p->crg + CRG70);
	udelay(1);
	value = (value | BIT(0)) & ~BIT(4);
	writel(value, p->crg + CRG70);
	value &= ~(BIT(17) | GENMASK(16, 12));
	writel(value, p->crg + CRG70);
	writel(value | (0x10 << 12), p->crg + CRG70);
	writel(value | (0x10 << 12) | BIT(17), p->crg + CRG70);
	writel(2, p->aiao + 0x34);
	if (!readl(p->aiao + 0x0c)) return -ENODEV;
	hrtimer_init(&p->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	p->timer.function = mv100_tick;
	ret = devm_add_action_or_reset(&pdev->dev, mv100_cancel, p);
	if (ret) return ret;
	ret = snd_pcm_new(card, "MV100 HDMI PCM", 0, 1, 0, &pcm);
	if (ret < 0) return ret;
	pcm->private_data = p;
	strscpy(pcm->name, "HDMI stereo 48k", sizeof(pcm->name));
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &mv100_ops);
	ret = snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV, &pdev->dev, 65536, 65536);
	if (ret < 0) return ret;
	strscpy(card->driver, "MV100HDMI", sizeof(card->driver));
	strscpy(card->shortname, "MV100 HDMI", sizeof(card->shortname));
	strscpy(card->longname, "Hi3798MV100 AIAO TX3 HDMI PCM", sizeof(card->longname));
	ret = snd_card_register(card);
	if (!ret) ret = devm_device_add_group(&pdev->dev, &mv100_group);
	if (!ret) dev_info(&pdev->dev, "HDMI PCM registered: TX3, S16_LE stereo 48000 Hz\n");
	return ret;
}

static const struct of_device_id mv100_of_match[] = {
	{ .compatible = "hisilicon,hi3798mv100-hdmi-pcm" }, {}
};
MODULE_DEVICE_TABLE(of, mv100_of_match);
static struct platform_driver mv100_driver = {
	.probe = mv100_probe,
	.driver = { .name = "mv100-hdmi-pcm", .of_match_table = mv100_of_match },
};
module_platform_driver(mv100_driver);
MODULE_DESCRIPTION("Hi3798MV100 HDMI PCM playback");
MODULE_LICENSE("GPL");
