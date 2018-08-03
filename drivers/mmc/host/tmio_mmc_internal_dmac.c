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

#define DM_CM_DTRAN_MODE	0x820
#define DM_CM_DTRAN_CTRL	0x828
#define DM_CM_RST		0x830
#define DM_CM_INFO1		0x840
#define DM_CM_INFO1_MASK	0x848
#define DM_CM_INFO2		0x850
#define DM_CM_INFO2_MASK	0x858
#define DM_DTRAN_ADDR		0x880

/* DM_CM_DTRAN_MODE */
#define DTRAN_MODE_CH_NUM_CH0	0	/* "downstream" = for write commands */
#define DTRAN_MODE_CH_NUM_CH1	BIT(16)	/* "uptream" = for read commands */
#define DTRAN_MODE_BUS_WIDTH	(BIT(5) | BIT(4))
#define DTRAN_MODE_ADDR_MODE	BIT(0)	/* 1 = Increment address */

/* DM_CM_DTRAN_CTRL */
#define DTRAN_CTRL_DM_START	BIT(0)

/* DM_CM_RST */
#define RST_DTRANRST1		BIT(9)
#define RST_DTRANRST0		BIT(8)
#define RST_RESERVED_BITS	GENMASK_ULL(32, 0)

/* DM_CM_INFO1 and DM_CM_INFO1_MASK */
#define INFO1_CLEAR		0
#define INFO1_DTRANEND1		BIT(17)
#define INFO1_DTRANEND0		BIT(16)

/* DM_CM_INFO2 and DM_CM_INFO2_MASK */
#define INFO2_DTRANERR1		BIT(17)
#define INFO2_DTRANERR0		BIT(16)

/*
 * Specification of this driver:
 * - host->chan_{rx,tx} will be used as a flag of enabling/disabling the dma
 * - Since this SDHI DMAC register set has 16 but 32-bit width, we
 *   need a custom accessor.
 */

static unsigned long global_flags;
void tmio_mmc_internal_dmac_global_flags_register(unsigned long flags)
{
	global_flags |= flags;
}
EXPORT_SYMBOL_GPL(tmio_mmc_internal_dmac_global_flags_register);

static void tmio_mmc_internal_dmac_dm_write(struct tmio_mmc_host *host,
					    int addr, u64 val)
{
	writeq(val, host->ctl + addr);
}

static void tmio_mmc_internal_dmac_enable_dma(struct tmio_mmc_host *host,
					      bool enable)
{
	struct renesas_sdhi *priv = host_to_priv(host);

	if (!host->chan_tx || !host->chan_rx)
		return;

	if (!enable)
		tmio_mmc_internal_dmac_dm_write(host, DM_CM_INFO1, INFO1_CLEAR);

	if (priv->dma_priv.enable)
		priv->dma_priv.enable(host, enable);
}

static void
tmio_mmc_internal_dmac_abort_dma(struct tmio_mmc_host *host) {
	u64 val = RST_DTRANRST1 | RST_DTRANRST0;

	tmio_mmc_internal_dmac_enable_dma(host, false);

	tmio_mmc_internal_dmac_dm_write(host, DM_CM_RST,
					RST_RESERVED_BITS & ~val);
	tmio_mmc_internal_dmac_dm_write(host, DM_CM_RST,
					RST_RESERVED_BITS | val);

	clear_bit(SDHI_INTERNAL_DMAC_RX_IN_USE, &global_flags);

	tmio_mmc_internal_dmac_enable_dma(host, true);
}

static void
tmio_mmc_internal_dmac_dataend_dma(struct tmio_mmc_host *host) {
	struct renesas_sdhi *priv = host_to_priv(host);

	tasklet_schedule(&priv->dma_priv.dma_complete);
}

static void
tmio_mmc_internal_dmac_start_dma(struct tmio_mmc_host *host,
				 struct mmc_data *data)
{
	struct scatterlist *sg = host->sg_ptr;
	u32 dtran_mode = DTRAN_MODE_BUS_WIDTH | DTRAN_MODE_ADDR_MODE;

	if (!dma_map_sg(&host->pdev->dev, sg, host->sg_len,
			mmc_get_dma_dir(data)))
		goto force_pio;

	/* This DMAC cannot handle if buffer is not 8-bytes alignment */
	if (!IS_ALIGNED(sg_dma_address(sg), 8))
		goto force_pio_with_unmap;

	if (data->flags & MMC_DATA_READ) {
		dtran_mode |= DTRAN_MODE_CH_NUM_CH1;
		if (test_bit(SDHI_INTERNAL_DMAC_ONE_RX_ONLY, &global_flags) &&
		    test_and_set_bit(SDHI_INTERNAL_DMAC_RX_IN_USE, &global_flags))
			goto force_pio_with_unmap;
	} else {
		dtran_mode |= DTRAN_MODE_CH_NUM_CH0;
	}

	tmio_mmc_internal_dmac_enable_dma(host, true);

	/* set dma parameters */
	tmio_mmc_internal_dmac_dm_write(host, DM_CM_DTRAN_MODE,
					    dtran_mode);
	tmio_mmc_internal_dmac_dm_write(host, DM_DTRAN_ADDR,
					    sg_dma_address(sg));

	return;

force_pio_with_unmap:
	dma_unmap_sg(&host->pdev->dev, sg, host->sg_len, mmc_get_dma_dir(data));

force_pio:
	host->force_pio = true;
	tmio_mmc_internal_dmac_enable_dma(host, false);
}

static void tmio_mmc_internal_dmac_issue_tasklet_fn(unsigned long arg)
{
	struct tmio_mmc_host *host = (struct tmio_mmc_host *)arg;

	tmio_mmc_enable_mmc_irqs(host, TMIO_STAT_DATAEND);

	/* start the DMAC */
	tmio_mmc_internal_dmac_dm_write(host, DM_CM_DTRAN_CTRL,
					    DTRAN_CTRL_DM_START);
}

static void tmio_mmc_internal_dmac_complete_tasklet_fn(unsigned long arg)
{
	struct tmio_mmc_host *host = (struct tmio_mmc_host *)arg;
	enum dma_data_direction dir;

	spin_lock_irq(&host->lock);

	if (!host->data)
		goto out;

	if (host->data->flags & MMC_DATA_READ)
		dir = DMA_FROM_DEVICE;
	else
		dir = DMA_TO_DEVICE;

	tmio_mmc_internal_dmac_enable_dma(host, false);
	dma_unmap_sg(&host->pdev->dev, host->sg_ptr, host->sg_len, dir);

	if (dir == DMA_FROM_DEVICE)
		clear_bit(SDHI_INTERNAL_DMAC_RX_IN_USE, &global_flags);

	tmio_mmc_do_data_irq(host);
out:
	spin_unlock_irq(&host->lock);
}

static void tmio_mmc_internal_dmac_request_dma(struct tmio_mmc_host *host,
					       struct tmio_mmc_data *pdata)
{
	struct renesas_sdhi *priv = host_to_priv(host);

	/* Each value is set to non-zero to assume "enabling" each DMA */
	host->chan_rx = host->chan_tx = (void *)0xdeadbeaf;

	tasklet_init(&priv->dma_priv.dma_complete,
		     tmio_mmc_internal_dmac_complete_tasklet_fn,
		     (unsigned long)host);
	tasklet_init(&host->dma_issue,
		     tmio_mmc_internal_dmac_issue_tasklet_fn,
		     (unsigned long)host);
}

static void
tmio_mmc_internal_dmac_release_dma(struct tmio_mmc_host *host)
{
	/* Each value is set to zero to assume "disabling" each DMA */
	host->chan_rx = host->chan_tx = NULL;
}

const struct tmio_mmc_dma_ops tmio_mmc_internal_dmac_dma_ops = {
	.start = tmio_mmc_internal_dmac_start_dma,
	.enable = tmio_mmc_internal_dmac_enable_dma,
	.request = tmio_mmc_internal_dmac_request_dma,
	.release = tmio_mmc_internal_dmac_release_dma,
	.abort = tmio_mmc_internal_dmac_abort_dma,
	.dataend = tmio_mmc_internal_dmac_dataend_dma,
};
EXPORT_SYMBOL_GPL(tmio_mmc_internal_dmac_dma_ops);
