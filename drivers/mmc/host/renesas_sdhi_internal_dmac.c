/*
 * DMA support for Internal DMAC with SDHI SD/SDIO controller
 *
 * Copyright (C) 2016-17 Renesas Electronics Corporation
 * Copyright (C) 2016-17 Horms Solutions, Simon Horman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>
#include <linux/sys_soc.h>

#include "renesas_sdhi.h"
#include "tmio_mmc.h"

/* Definitions for sampling clocks */
static struct renesas_sdhi_scc rcar_gen3_scc_taps[] = {
	{
		.clk_rate = 0,
		.tap = 0x00000300,
	},
};

static const struct renesas_sdhi_of_data of_rcar_gen3_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_CLK_ACTUAL |
			  TMIO_MMC_HAVE_CBSY | TMIO_MMC_MIN_RCAR2,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_CMD23,
	.capabilities2	= MMC_CAP2_NO_WRITE_PROTECT,
	.bus_shift	= 2,
	.scc_offset	= 0x1000,
	.taps		= rcar_gen3_scc_taps,
	.taps_num	= ARRAY_SIZE(rcar_gen3_scc_taps),
	/* DMAC can handle 0xffffffff blk count but only 1 segment */
	.max_blk_count	= 0xffffffff,
	.max_segs	= 1,
};

static const struct of_device_id renesas_sdhi_internal_dmac_of_match[] = {
	{ .compatible = "renesas,sdhi-r8a7795", .data = &of_rcar_gen3_compatible, },
	{ .compatible = "renesas,sdhi-r8a7796", .data = &of_rcar_gen3_compatible, },
	{ .compatible = "renesas,rcar-gen3-sdhi", .data = &of_rcar_gen3_compatible, },
	{},
};
MODULE_DEVICE_TABLE(of, renesas_sdhi_internal_dmac_of_match);

/*
 * Whitelist of specific R-Car Gen3 SoC ES versions to use this DMAC
 * implementation as others may use a different implementation.
 */
static const struct soc_device_attribute gen3_soc_whitelist[] = {
	/* specific ones */
	{ .soc_id = "r8a7795", .revision = "ES1.*",
	  .data = (void *)BIT(SDHI_INTERNAL_DMAC_ONE_RX_ONLY) },
	{ .soc_id = "r8a7796", .revision = "ES1.0",
	  .data = (void *)BIT(SDHI_INTERNAL_DMAC_ONE_RX_ONLY) },
	/* generic ones */
	{ .soc_id = "r8a7795" },
	{ .soc_id = "r8a7796" },
	{ .soc_id = "r8a77965" },
	{ .soc_id = "r8a77980" },
	{ .soc_id = "r8a77995" },
	{ /* sentinel */ }
};

static int renesas_sdhi_internal_dmac_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *soc = soc_device_match(gen3_soc_whitelist);

	if (!soc)
		return -ENODEV;

	tmio_mmc_internal_dmac_global_flags_register((unsigned long)soc->data);

	return renesas_sdhi_probe(pdev, &tmio_mmc_internal_dmac_dma_ops);
}

static const struct dev_pm_ops renesas_sdhi_internal_dmac_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(tmio_mmc_host_runtime_suspend,
			   tmio_mmc_host_runtime_resume,
			   NULL)
};

static struct platform_driver renesas_internal_dmac_sdhi_driver = {
	.driver		= {
		.name	= "renesas_sdhi_internal_dmac",
		.pm	= &renesas_sdhi_internal_dmac_dev_pm_ops,
		.of_match_table = renesas_sdhi_internal_dmac_of_match,
	},
	.probe		= renesas_sdhi_internal_dmac_probe,
	.remove		= renesas_sdhi_remove,
};

module_platform_driver(renesas_internal_dmac_sdhi_driver);

MODULE_DESCRIPTION("Renesas SDHI driver for internal DMAC");
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_LICENSE("GPL v2");
