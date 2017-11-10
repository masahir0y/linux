// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "tmio_mmc.h"

/* UniPhier specific registers? */
#define UNIPHIER_SD_HOST_MODE		0x1c8

#define UNIPHIER_SD_VOLT		0x1e4
#define   UNIPHIER_SD_VOLT_MASK			GENMASK(1, 0)
#define   UNIPHIER_SD_VOLT_OFF			0
#define   UNIPHIER_SD_VOLT_330			1	/* 3.3V signal */
#define   UNIPHIER_SD_VOLT_180			2	/* 1.8V signal */

#define UNIPHIER_SD_DMA_MODE		0x410
#define   UNIPHIER_SD_DMA_MODE_DIR_MASK		GENMASK(17, 16)
#define   UNIPHIER_SD_DMA_MODE_DIR_TO_DEV	0
#define   UNIPHIER_SD_DMA_MODE_DIR_FROM_DEV	1
#define   UNIPHIER_SD_DMA_MODE_WIDTH_MASK	GENMASK(5, 4)
#define   UNIPHIER_SD_DMA_MODE_WIDTH_8		0
#define   UNIPHIER_SD_DMA_MODE_WIDTH_16		1
#define   UNIPHIER_SD_DMA_MODE_WIDTH_32		2
#define   UNIPHIER_SD_DMA_MODE_WIDTH_64		3
#define   UNIPHIER_SD_DMA_MODE_ADDR_INC		BIT(0)	/* 1: inc, 0: fixed */
#define UNIPHIER_SD_DMA_CTL		0x414
#define   UNIPHIER_SD_DMA_CTL_START	BIT(0)	/* start DMA (auto cleared) */
#define UNIPHIER_SD_DMA_ADDR_L		0x440
#define UNIPHIER_SD_DMA_ADDR_H		0x444

#define UNIPHIER_SD_DMA_INFO1		0x420
#define UNIPHIER_SD_DMA_INFO1_MASK	0x424
#define   UNIPHIER_SD_DMA_INFO1_END_RD2	BIT(20)	/* DMA from device is complete*/
#define   UNIPHIER_SD_DMA_INFO1_END_RD	BIT(17)	/* Don't use!  Hardware bug */
#define   UNIPHIER_SD_DMA_INFO1_END_WR	BIT(16)	/* DMA to device is complete */

#define MMC_CAP_UHS		(MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | \
				 MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104 | \
				 MMC_CAP_UHS_DDR50)

struct uniphier_sd_priv {
	struct tmio_mmc_data tmio_data;
	struct clk *clk;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinstate_default;
	struct pinctrl_state *pinstate_uhs;
	enum dma_data_direction dma_dir;
	unsigned int version;
};

irqreturn_t uniphier_sd_irq(int irq, void *devid)
{
	struct tmio_mmc_host *host = devid;
	u32 status;

	status = readl(host->ctl + UNIPHIER_SD_DMA_INFO1);

	writel(0, host->ctl + UNIPHIER_SD_DMA_INFO1);

	return IRQ_HANDLED;
}

static void *uniphier_sd_priv(struct tmio_mmc_host *host)
{
	return container_of(host->pdata, struct uniphier_sd_priv, tmio_data);
}

static void uniphier_sd_dma_endisable(struct tmio_mmc_host *host, int enable)
{
	sd_ctrl_write16(host, CTL_DMA_ENABLE, DMA_ENABLE_DMASDRW);
}

static void uniphier_sd_dma_issue(unsigned long arg)
{
	struct tmio_mmc_host *host = (void *)arg;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	tmio_mmc_enable_mmc_irqs(host, TMIO_STAT_DATAEND);
	spin_unlock_irqrestore(&host->lock, flags);

	uniphier_sd_dma_endisable(host, 1);
	writel(UNIPHIER_SD_DMA_CTL_START, host->ctl + UNIPHIER_SD_DMA_CTL);
}

static void uniphier_sd_dma_start(struct tmio_mmc_host *host,
				  struct mmc_data *data)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	struct scatterlist *sg = host->sg_ptr;
	dma_addr_t dma_addr;
	unsigned int dma_mode_dir;
	u32 dma_mode;
	int sg_len;
	u32 dma_irq;

	if (WARN_ON(host->sg_len != 1))
		goto force_pio;

	if (!IS_ALIGNED(sg->offset, 8))
		goto force_pio;

	if (data->flags & MMC_DATA_READ) {
		priv->dma_dir = DMA_FROM_DEVICE;
		dma_mode_dir = UNIPHIER_SD_DMA_MODE_DIR_FROM_DEV;
		dma_irq = UNIPHIER_SD_DMA_INFO1_END_RD2;
	} else {
		priv->dma_dir = DMA_TO_DEVICE;
		dma_mode_dir = UNIPHIER_SD_DMA_MODE_DIR_TO_DEV;
		dma_irq = UNIPHIER_SD_DMA_INFO1_END_WR;
	}

	sg_len = dma_map_sg(mmc_dev(host->mmc), sg, 1, priv->dma_dir);
	if (sg_len != 1)
		goto force_pio;

	dma_mode = FIELD_PREP(UNIPHIER_SD_DMA_MODE_DIR_MASK, dma_mode_dir);
	dma_mode |= FIELD_PREP(UNIPHIER_SD_DMA_MODE_WIDTH_MASK,
			       UNIPHIER_SD_DMA_MODE_WIDTH_64);
	dma_mode |= UNIPHIER_SD_DMA_MODE_ADDR_INC;

	writel(dma_mode, host->ctl + UNIPHIER_SD_DMA_MODE);

	writel(~dma_irq, host->ctl + UNIPHIER_SD_DMA_INFO1_MASK);

	dma_addr = sg_dma_address(data->sg);
	writel(lower_32_bits(dma_addr), host->ctl + UNIPHIER_SD_DMA_ADDR_L);
	writel(upper_32_bits(dma_addr), host->ctl + UNIPHIER_SD_DMA_ADDR_H);

	return;
force_pio:
	dev_warn(mmc_dev(host->mmc),
		 "failed to set up DMA. falling back to PIO\n");
	host->force_pio = true;
	uniphier_sd_dma_endisable(host, 0);
}

static void uniphier_sd_dma_enable(struct tmio_mmc_host *host, bool enable)
{
}

static void uniphier_sd_dma_request(struct tmio_mmc_host *host,
				    struct tmio_mmc_data *pdata)
{
	/* Each value is set to non-zero to assume "enabling" each DMA */
	host->chan_rx = host->chan_tx = (void *)0xdeadbeaf;

	tasklet_init(&host->dma_issue, uniphier_sd_dma_issue,
		     (unsigned long)host);
}

static void uniphier_sd_dma_release(struct tmio_mmc_host *host)
{
	/* Each value is set to zero to assume "disabling" each DMA */
	host->chan_rx = host->chan_tx = NULL;
}

static void uniphier_sd_dma_abort(struct tmio_mmc_host *host)
{
}

static void uniphier_sd_dma_dataend(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);

	uniphier_sd_dma_endisable(host, 0);
	dma_unmap_sg(mmc_dev(host->mmc), host->sg_ptr, 1, priv->dma_dir);

	tmio_mmc_do_data_irq(host);
}

static const struct tmio_mmc_dma_ops uniphier_sd_dma_ops = {
	.start = uniphier_sd_dma_start,
	.enable = uniphier_sd_dma_enable,
	.request = uniphier_sd_dma_request,
	.release = uniphier_sd_dma_release,
	.abort = uniphier_sd_dma_abort,
	.dataend = uniphier_sd_dma_dataend,
};

static int uniphier_sd_clk_enable(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	struct mmc_host *mmc = host->mmc;
	unsigned long rate;
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	ret = clk_set_rate(priv->clk, ULONG_MAX);
	if (ret)
		return ret;

	rate = clk_get_rate(priv->clk);

	/*
	 * If the clock driver returns zero frequency, do not set it.
	 * Let's hope mmc->f_max has been set by "max-frequency" DT property.
	 */
	if (rate)
		mmc->f_max = rate;

	mmc->f_min = mmc->f_max / 1024;

	return 0;
}

static void uniphier_sd_host_init(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);

	/*
	 * Connected to 32bit AXI.
	 * This register dropped backward compatibility at version 0x10.
	 * Write an appropriate value depending on the IP version.
	 */
	writel(priv->version >= 0x10 ? 0x00000101 : 0x00000000,
	       host->ctl + UNIPHIER_SD_HOST_MODE);
}

static int uniphier_sd_start_signal_voltage_switch(struct mmc_host *mmc,
						   struct mmc_ios *ios)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	struct pinctrl_state *pinstate;
	u32 val, tmp;

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		val = UNIPHIER_SD_VOLT_330;
		pinstate = priv->pinstate_default;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		val = UNIPHIER_SD_VOLT_180;
		pinstate = priv->pinstate_uhs;
		break;
	default:
		return -ENOTSUPP;
	}

	tmp = readl(host->ctl + UNIPHIER_SD_VOLT);
	tmp &= ~UNIPHIER_SD_VOLT_MASK;
	tmp |= FIELD_PREP(UNIPHIER_SD_VOLT_MASK, val);
	writel(tmp, host->ctl + UNIPHIER_SD_VOLT);

	pinctrl_select_state(priv->pinctrl, pinstate);

	return 0;
}

static int uniphier_sd_uhs_init(struct tmio_mmc_host *host,
				struct uniphier_sd_priv *priv)
{
	priv->pinctrl = devm_pinctrl_get(mmc_dev(host->mmc));
	if (IS_ERR(priv->pinctrl))
		return PTR_ERR(priv->pinctrl);

	priv->pinstate_default = pinctrl_lookup_state(priv->pinctrl,
						      PINCTRL_STATE_DEFAULT);
	if (IS_ERR(priv->pinstate_default))
		return PTR_ERR(priv->pinstate_default);

	priv->pinstate_uhs = pinctrl_lookup_state(priv->pinctrl, "uhs");
	if (IS_ERR(priv->pinstate_uhs))
		return PTR_ERR(priv->pinstate_uhs);

	host->ops.start_signal_voltage_switch =
					uniphier_sd_start_signal_voltage_switch;

	return 0;
}

#define UNIPHIER_SD_VERSION		0x1c4	/* version register */

static int uniphier_sd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_sd_priv *priv;
	struct tmio_mmc_data *tmio_data;
	struct tmio_mmc_host *host;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to get IRQ number");
		return irq;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(priv->clk);
	}

	tmio_data = &priv->tmio_data;

	host = tmio_mmc_host_alloc(pdev, tmio_data);
	if (IS_ERR(host))
		return PTR_ERR(host);

	if (host->mmc->caps & MMC_CAP_UHS) {
		ret = uniphier_sd_uhs_init(host, priv);
		if (ret) {
			dev_warn(dev,
				 "failed to setup UHS (error %d).  Disabling UHS.",
				 ret);
			host->mmc->caps &= ~MMC_CAP_UHS;
		}
	}

	ret = devm_request_irq(dev, irq, tmio_mmc_irq, IRQF_SHARED, dev_name(dev), host);
	if (ret)
		goto free_host;

	ret = devm_request_irq(dev, irq, uniphier_sd_irq, IRQF_SHARED, dev_name(dev), host);
	if (ret)
		goto free_host;

	host->bus_shift = 1;
	host->clk_enable = uniphier_sd_clk_enable;
	host->dma_ops = &uniphier_sd_dma_ops;

	ret = uniphier_sd_clk_enable(host);
	if (ret)
		goto free_host;

	priv->version = sd_ctrl_read16(host, CTL_VERSION);
	dev_info(dev, "version %x\n", priv->version);

	uniphier_sd_host_init(host);

	tmio_data->ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34;
	tmio_data->max_segs = 1;
	tmio_data->max_blk_count = U16_MAX;

	ret = tmio_mmc_host_probe(host);
	if (ret)
		goto free_host;

	return 0;

free_host:
	tmio_mmc_host_free(host);

	return ret;
}

static int uniphier_sd_remove(struct platform_device *pdev)
{
	struct tmio_mmc_host *host = platform_get_drvdata(pdev);

	tmio_mmc_host_remove(host);

	return 0;
}

static const struct of_device_id uniphier_sd_match[] = {
	{ .compatible = "socionext,uniphier-sd" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_sd_match);

static struct platform_driver uniphier_sd_driver = {
	.probe = uniphier_sd_probe,
	.remove = uniphier_sd_remove,
	.driver = {
		.name = "uniphier-sd",
		.of_match_table = uniphier_sd_match,
	},
};
module_platform_driver(uniphier_sd_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier SD/eMMC host controller driver");
MODULE_LICENSE("GPL v2");
