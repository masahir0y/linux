// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "tmio_mmc.h"

/* UniPhier specific registers? */
#define CTL_SD_VOLT		0x1e4	/* voltage switch */
#define   CTL_SD_VOLT_MASK		GENMASK(1, 0)
#define   CTL_SD_VOLT_OFF		0
#define   CTL_SD_VOLT_330		1	/* 3.3V signal */
#define   CTL_SD_VOLT_180		2	/* 1.8V signal */


#define MMC_CAP_UHS		(MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | \
				 MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104 | \
				 MMC_CAP_UHS_DDR50)

struct uniphier_sd_priv {
	struct tmio_mmc_data tmio_data;
	struct clk *clk;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinstate_default;
	struct pinctrl_state *pinstate_uhs;

};

static void *uniphier_sd_priv(struct tmio_mmc_host *host)
{
	return container_of(host->pdata, struct uniphier_sd_priv, tmio_data);
}

static void uniphier_sd_dma_start(struct tmio_mmc_host *host,
				  struct mmc_data *data)
{
}

static void uniphier_sd_dma_enable(struct tmio_mmc_host *host, bool enable)
{
}

static void uniphier_sd_dma_request(struct tmio_mmc_host *host,
				    struct tmio_mmc_data *pdata)
{
}

static void uniphier_sd_dma_release(struct tmio_mmc_host *host)
{
}

static void uniphier_sd_dma_abort(struct tmio_mmc_host *host)
{
}

static void uniphier_sd_dma_dataend(struct tmio_mmc_host *host)
{
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

static int uniphier_sd_start_signal_voltage_switch(struct mmc_host *mmc,
						   struct mmc_ios *ios)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	struct pinctrl_state *pinstate;
	u16 val, tmp;

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		val = CTL_SD_VOLT_330;
		pinstate = priv->pinstate_default;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		val = CTL_SD_VOLT_180;
		pinstate = priv->pinstate_uhs;
		break;
	default:
		return -ENOTSUPP;
	}

	tmp = sd_ctrl_read16(host, CTL_SD_VOLT);
	tmp &= ~CTL_SD_VOLT_MASK;
	tmp |= FIELD_PREP(CTL_SD_VOLT_MASK, val);
	sd_ctrl_write16(host, CTL_SD_VOLT, tmp);

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

	host->mmc_host_ops.start_signal_voltage_switch =
					uniphier_sd_start_signal_voltage_switch;

	return 0;
}

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

	host = tmio_mmc_host_alloc(pdev);
	if (!host)
		ret = -ENOMEM;

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto free_host;

	if (host->mmc->caps & MMC_CAP_UHS) {
		ret = uniphier_sd_uhs_init(host, priv);
		if (ret) {
			dev_warn(dev,
				 "failed to setup UHS (error %d).  Disabling UHS.",
				 ret);
			host->mmc->caps &= ~MMC_CAP_UHS;
		}
	}

	ret = devm_request_irq(dev, irq, tmio_mmc_irq, 0, dev_name(dev), host);
	if (ret)
		goto free_host;

	host->bus_shift = 1;
	host->clk_enable = uniphier_sd_clk_enable;

	tmio_data = &priv->tmio_data;

	tmio_data->ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34;
	tmio_data->max_blk_count = U32_MAX;

	ret = tmio_mmc_host_probe(host, tmio_data, &uniphier_sd_dma_ops);
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
MODULE_LICENSE("GPL");
