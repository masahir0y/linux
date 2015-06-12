/*
 * Copyright (C) 2017 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#define UNIPHIER_SD_CMD			0x000	/* command */
#define   UNIPHIER_SD_CMD_NOSTOP	BIT(14)	/* No automatic CMD12 issue */
#define   UNIPHIER_SD_CMD_MULTI		BIT(13)	/* multiple block transfer */
#define   UNIPHIER_SD_CMD_RD		BIT(12)	/* 1: read, 0: write */
#define   UNIPHIER_SD_CMD_DATA		BIT(11)	/* data transfer */
#define   UNIPHIER_SD_CMD_APP		BIT(6)	/* ACMD preceded by CMD55 */
#define   UNIPHIER_SD_CMD_NORMAL	(0 << 8)/* auto-detect of resp-type */
#define   UNIPHIER_SD_CMD_RSP_NONE	(3 << 8)/* response: none */
#define   UNIPHIER_SD_CMD_RSP_R1	(4 << 8)/* response: R1, R5, R6, R7 */
#define   UNIPHIER_SD_CMD_RSP_R1B	(5 << 8)/* response: R1b, R5b */
#define   UNIPHIER_SD_CMD_RSP_R2	(6 << 8)/* response: R2 */
#define   UNIPHIER_SD_CMD_RSP_R3	(7 << 8)/* response: R3, R4 */
#define UNIPHIER_SD_ARG			0x008	/* command argument */
#define UNIPHIER_SD_STOP		0x010	/* stop action control */
#define   UNIPHIER_SD_STOP_SEC		BIT(8)	/* use sector count */
#define   UNIPHIER_SD_STOP_STP		BIT(0)	/* issue CMD12 */
#define UNIPHIER_SD_SECCNT		0x014	/* sector counter */
#define UNIPHIER_SD_RSP10		0x018	/* response[39:8] */
#define UNIPHIER_SD_RSP32		0x020	/* response[71:40] */
#define UNIPHIER_SD_RSP54		0x028	/* response[103:72] */
#define UNIPHIER_SD_RSP76		0x030	/* response[127:104] */
#define UNIPHIER_SD_INFO1		0x038	/* IRQ status 1 */
#define   UNIPHIER_SD_INFO1_CD		BIT(5)	/* state of card detect */
#define   UNIPHIER_SD_INFO1_INSERT	BIT(4)	/* card inserted */
#define   UNIPHIER_SD_INFO1_REMOVE	BIT(3)	/* card removed */
#define   UNIPHIER_SD_INFO1_CMP		BIT(2)	/* data complete */
#define   UNIPHIER_SD_INFO1_RSP		BIT(0)	/* response complete */
#define UNIPHIER_SD_INFO2		0x03c	/* IRQ status 2 */
#define   UNIPHIER_SD_INFO2_ERR_ILA	BIT(15)	/* illegal access err */
#define   UNIPHIER_SD_INFO2_CBSY	BIT(14)	/* command busy */
#define   UNIPHIER_SD_INFO2_BWE		BIT(9)	/* write buffer ready */
#define   UNIPHIER_SD_INFO2_BRE		BIT(8)	/* read buffer ready */
#define   UNIPHIER_SD_INFO2_DAT0	BIT(7)	/* SDDAT0 */
#define   UNIPHIER_SD_INFO2_ERR_RTO	BIT(6)	/* response time out */
#define   UNIPHIER_SD_INFO2_ERR_ILR	BIT(5)	/* illegal read err */
#define   UNIPHIER_SD_INFO2_ERR_ILW	BIT(4)	/* illegal write err */
#define   UNIPHIER_SD_INFO2_ERR_TO	BIT(3)	/* time out error */
#define   UNIPHIER_SD_INFO2_ERR_END	BIT(2)	/* END bit error */
#define   UNIPHIER_SD_INFO2_ERR_CRC	BIT(1)	/* CRC error */
#define   UNIPHIER_SD_INFO2_ERR_IDX	BIT(0)	/* cmd index error */
#define UNIPHIER_SD_INFO1_MASK		0x040
#define UNIPHIER_SD_INFO2_MASK		0x044
#define UNIPHIER_SD_CLKCTL		0x048	/* clock divisor */
#define   UNIPHIER_SD_CLKCTL_DIV_MASK	0x104ff
#define   UNIPHIER_SD_CLKCTL_DIV1024	BIT(16)	/* SDCLK = CLK / 1024 */
#define   UNIPHIER_SD_CLKCTL_DIV512	BIT(7)	/* SDCLK = CLK / 512 */
#define   UNIPHIER_SD_CLKCTL_DIV256	BIT(6)	/* SDCLK = CLK / 256 */
#define   UNIPHIER_SD_CLKCTL_DIV128	BIT(5)	/* SDCLK = CLK / 128 */
#define   UNIPHIER_SD_CLKCTL_DIV64	BIT(4)	/* SDCLK = CLK / 64 */
#define   UNIPHIER_SD_CLKCTL_DIV32	BIT(3)	/* SDCLK = CLK / 32 */
#define   UNIPHIER_SD_CLKCTL_DIV16	BIT(2)	/* SDCLK = CLK / 16 */
#define   UNIPHIER_SD_CLKCTL_DIV8	BIT(1)	/* SDCLK = CLK / 8 */
#define   UNIPHIER_SD_CLKCTL_DIV4	BIT(0)	/* SDCLK = CLK / 4 */
#define   UNIPHIER_SD_CLKCTL_DIV2	0	/* SDCLK = CLK / 2 */
#define   UNIPHIER_SD_CLKCTL_DIV1	BIT(10)	/* SDCLK = CLK */
#define   UNIPHIER_SD_CLKCTL_OFFEN	BIT(9)	/* stop SDCLK when unused */
#define   UNIPHIER_SD_CLKCTL_SCLKEN	BIT(8)	/* SDCLK output enable */
#define UNIPHIER_SD_SIZE		0x04c	/* block size */
#define UNIPHIER_SD_OPTION		0x050
#define   UNIPHIER_SD_OPTION_WIDTH_MASK	(5 << 13)
#define   UNIPHIER_SD_OPTION_WIDTH_1	(4 << 13)
#define   UNIPHIER_SD_OPTION_WIDTH_4	(0 << 13)
#define   UNIPHIER_SD_OPTION_WIDTH_8	(1 << 13)
#define UNIPHIER_SD_BUF			0x060	/* read/write buffer */
#define UNIPHIER_SD_EXTMODE		0x1b0
#define   UNIPHIER_SD_EXTMODE_DMA_EN	BIT(1)	/* transfer 1: DMA, 0: pio */
#define UNIPHIER_SD_SOFT_RST		0x1c0
#define UNIPHIER_SD_SOFT_RST_RSTX	BIT(0)	/* reset deassert */
#define UNIPHIER_SD_VERSION		0x1c4	/* version register */
#define UNIPHIER_SD_VERSION_IP		0xff	/* IP version */
#define UNIPHIER_SD_HOST_MODE		0x1c8
#define UNIPHIER_SD_IF_MODE		0x1cc
#define   UNIPHIER_SD_IF_MODE_DDR	BIT(0)	/* DDR mode */
#define UNIPHIER_SD_VOLT		0x1e4	/* voltage switch */
#define   UNIPHIER_SD_VOLT_MASK		(3 << 0)
#define   UNIPHIER_SD_VOLT_OFF		(0 << 0)
#define   UNIPHIER_SD_VOLT_330		(1 << 0)/* 3.3V signal */
#define   UNIPHIER_SD_VOLT_180		(2 << 0)/* 1.8V signal */
#define UNIPHIER_SD_DMA_MODE		0x410
#define   UNIPHIER_SD_DMA_MODE_DIR_RD	BIT(16)	/* 1: from device, 0: to dev */
#define   UNIPHIER_SD_DMA_MODE_ADDR_INC	BIT(0)	/* 1: address inc, 0: fixed */
#define UNIPHIER_SD_DMA_CTL		0x414
#define   UNIPHIER_SD_DMA_CTL_START	BIT(0)	/* start DMA (auto cleared) */
#define UNIPHIER_SD_DMA_RST		0x418
#define   UNIPHIER_SD_DMA_RST_RD	BIT(9)
#define   UNIPHIER_SD_DMA_RST_WR	BIT(8)
#define UNIPHIER_SD_DMA_INFO1		0x420
#define   UNIPHIER_SD_DMA_INFO1_END_RD2	BIT(20)	/* DMA from device is complete*/
#define   UNIPHIER_SD_DMA_INFO1_END_RD	BIT(17)	/* Don't use!  Hardware bug */
#define   UNIPHIER_SD_DMA_INFO1_END_WR	BIT(16)	/* DMA to device is complete */
#define UNIPHIER_SD_DMA_INFO1_MASK	0x424
#define UNIPHIER_SD_DMA_INFO2		0x428
#define   UNIPHIER_SD_DMA_INFO2_ERR_RD	BIT(17)
#define   UNIPHIER_SD_DMA_INFO2_ERR_WR	BIT(16)
#define UNIPHIER_SD_DMA_INFO2_MASK	0x42c
#define UNIPHIER_SD_DMA_ADDR_L		0x440
#define UNIPHIER_SD_DMA_ADDR_H		0x444

struct uniphier_sd_priv {
	struct mmc_host *host;
	struct mmc_command *cmd;
	enum dma_data_direction dma_dir;
	u32 *pio_buf;
	unsigned int pio_len;
	struct dma_chan *dma_ext;
	void __iomem *regbase;
	unsigned int irq_stat_reg;
	unsigned int irq_mask_reg;
	unsigned int irq_bits;
	void (*irq_handler)(struct uniphier_sd_priv *);
	bool irq_thread;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinstate_default;
	struct pinctrl_state *pinstate_1_8_v;
	struct clk *clk;
	struct clk *clk_hw_reset;
	unsigned long mclk;
	unsigned int version;
	u32 caps;
#define UNIPHIER_SD_CAP_VDD_330		BIT(0)	/* support 3.3V signaling */
#define UNIPHIER_SD_CAP_VDD_180		BIT(1)	/* support 1.8V signaling */
#define UNIPHIER_SD_CAP_VOLTAGE_SWITCH	BIT(3)	/* can switch voltage */
#define UNIPHIER_SD_CAP_DMA_AVAIL	BIT(4)	/* DMA is available */
#define UNIPHIER_SD_CAP_DMA_INTERNAL	BIT(5)	/* have internal DMA engine */
};

static void uniphier_sd_clear_irq(struct uniphier_sd_priv *priv,
				  unsigned int stat_reg, unsigned int bits)
{
	u32 tmp;

	tmp = readl(priv->regbase + stat_reg);
	tmp &= ~bits;
	writel(tmp, priv->regbase + stat_reg);
}

static void uniphier_sd_clear_current_irq(struct uniphier_sd_priv *priv)
{
	uniphier_sd_clear_irq(priv, priv->irq_stat_reg, priv->irq_bits);
}

/* clear all IRQs except card detection */
static void uniphier_sd_clear_all_irqs(struct uniphier_sd_priv *priv)
{
	u32 tmp;

	tmp = readl(priv->regbase + UNIPHIER_SD_INFO1);
	tmp &= ~(UNIPHIER_SD_INFO1_CMP | UNIPHIER_SD_INFO1_RSP);
	writel(tmp, priv->regbase + UNIPHIER_SD_INFO1);

	writel(0, priv->regbase + UNIPHIER_SD_INFO2);

	if (priv->caps & UNIPHIER_SD_CAP_DMA_INTERNAL) {
		writel(0, priv->regbase + UNIPHIER_SD_DMA_INFO1);
		writel(0, priv->regbase + UNIPHIER_SD_DMA_INFO2);
	}
}

/* disable all IRQs except card detection */
static void uniphier_sd_disable_all_irqs(struct uniphier_sd_priv *priv)
{
	u32 tmp;

	tmp = readl(priv->regbase + UNIPHIER_SD_INFO1_MASK);
	tmp |= UNIPHIER_SD_INFO1_CMP | UNIPHIER_SD_INFO1_RSP;
	writel(tmp, priv->regbase + UNIPHIER_SD_INFO1_MASK);

	writel(U32_MAX, priv->regbase + UNIPHIER_SD_INFO2_MASK);

	if (priv->caps & UNIPHIER_SD_CAP_DMA_INTERNAL) {
		writel(U32_MAX, priv->regbase + UNIPHIER_SD_DMA_INFO1_MASK);
		writel(U32_MAX, priv->regbase + UNIPHIER_SD_DMA_INFO2_MASK);
	}
}

static void uniphier_sd_enable_err_irqs(struct uniphier_sd_priv *priv)
{
	u32 tmp;

	tmp = readl(priv->regbase + UNIPHIER_SD_INFO2_MASK);
	tmp &= ~(UNIPHIER_SD_INFO2_ERR_IDX | UNIPHIER_SD_INFO2_ERR_CRC |
		 UNIPHIER_SD_INFO2_ERR_END | UNIPHIER_SD_INFO2_ERR_TO |
		 UNIPHIER_SD_INFO2_ERR_ILW | UNIPHIER_SD_INFO2_ERR_ILR |
		 UNIPHIER_SD_INFO2_ERR_RTO | UNIPHIER_SD_INFO2_ERR_ILA);
	writel(tmp, priv->regbase + UNIPHIER_SD_INFO2_MASK);

	if (priv->caps & UNIPHIER_SD_CAP_DMA_INTERNAL)
		writel(0, priv->regbase + UNIPHIER_SD_DMA_INFO2_MASK);
}

static void uniphier_sd_enable_irq(struct uniphier_sd_priv *priv,
				   unsigned int stat_reg,
				   unsigned int mask_reg, u32 bits,
				   void (*handler)(struct uniphier_sd_priv *),
				   bool thread)
{
	u32 tmp;

	priv->irq_stat_reg = stat_reg;
	priv->irq_mask_reg = mask_reg;
	priv->irq_bits = bits;
	priv->irq_handler = handler;
	priv->irq_thread = thread;

	tmp = readl(priv->regbase + mask_reg);
	tmp &= ~bits;
	writel(tmp, priv->regbase + mask_reg);
}

static void uniphier_sd_request_done(struct uniphier_sd_priv *priv)
{
	uniphier_sd_disable_all_irqs(priv);
	uniphier_sd_clear_all_irqs(priv);

	mmc_request_done(priv->host, priv->cmd->mrq);
}

static bool uniphier_sd_detect_change(struct uniphier_sd_priv *priv)
{
	u32 info1 = readl(priv->regbase + UNIPHIER_SD_INFO1);
	bool changed = false;

	if (info1 & UNIPHIER_SD_INFO1_INSERT) {
		dev_dbg(mmc_dev(priv->host), "card inserted\n");
		changed = true;
	}

	if (info1 & UNIPHIER_SD_INFO1_REMOVE) {
		dev_dbg(mmc_dev(priv->host), "card removed\n");
		changed = true;
	}

	if (changed) {
		uniphier_sd_clear_irq(priv, UNIPHIER_SD_INFO1,
				      UNIPHIER_SD_INFO1_INSERT |
				      UNIPHIER_SD_INFO1_REMOVE);
		uniphier_sd_disable_all_irqs(priv);
		mmc_detect_change(priv->host, 0);
	}

	return changed;
}

static int uniphier_sd_check_error(struct uniphier_sd_priv *priv)
{
	struct device *dev = mmc_dev(priv->host);
	u32 info2 = readl(priv->regbase + UNIPHIER_SD_INFO2);

	if (info2 & UNIPHIER_SD_INFO2_ERR_RTO) {
		dev_dbg(dev, "response timeout\n");
		return -ETIMEDOUT;
	}

	if (info2 & UNIPHIER_SD_INFO2_ERR_TO) {
		dev_err(dev, "timeout error\n");
		return -ETIMEDOUT;
	}

	if (info2 & (UNIPHIER_SD_INFO2_ERR_END | UNIPHIER_SD_INFO2_ERR_CRC |
		     UNIPHIER_SD_INFO2_ERR_IDX)) {
		dev_err(dev, "communication out of sync\n");
		return -EILSEQ;
	}

	if (info2 & (UNIPHIER_SD_INFO2_ERR_ILA | UNIPHIER_SD_INFO2_ERR_ILR |
		     UNIPHIER_SD_INFO2_ERR_ILW)) {
		dev_err(dev, "illegal access\n");
		return -EIO;
	}

	if ((priv->caps & UNIPHIER_SD_CAP_DMA_INTERNAL) &&
	    readl(priv->regbase + UNIPHIER_SD_DMA_INFO2)) {
		dev_err(dev, "DMA error\n");
		return -EIO;
	}

	return 0;
}

static void uniphier_sd_cmd_done(struct uniphier_sd_priv *priv);

static irqreturn_t uniphier_sd_interrupt(int irq, void *dev_id)
{
	struct uniphier_sd_priv *priv = dev_id;
	int err;

	dev_dbg(mmc_dev(priv->host), "%s: info1 = %x, info2 = %x\n", __func__,
		readl(priv->regbase + UNIPHIER_SD_INFO1),
		readl(priv->regbase + UNIPHIER_SD_INFO2));

	if (uniphier_sd_detect_change(priv))
		return IRQ_HANDLED;

	err = uniphier_sd_check_error(priv);
	if (err) {
		priv->cmd->error = err;
		uniphier_sd_cmd_done(priv);
		return IRQ_HANDLED;
	}

	if (readl(priv->regbase + priv->irq_stat_reg) & priv->irq_bits) {
		uniphier_sd_clear_current_irq(priv);
		if (priv->irq_thread)
			return IRQ_WAKE_THREAD;
		priv->irq_handler(priv);
		return IRQ_HANDLED;
	}

	dev_dbg(mmc_dev(priv->host), "IRQ was not handled\n");

	return IRQ_NONE;
}

static irqreturn_t uniphier_sd_interrupt_t(int irq, void *dev_id)
{
	struct uniphier_sd_priv *priv = dev_id;

	priv->irq_handler(dev_id);

	return IRQ_HANDLED;
}

static void uniphier_sd_xfer_done(struct uniphier_sd_priv *priv)
{
	struct mmc_data *data = priv->cmd->data;

	data->bytes_xfered = data->blksz * data->blocks;

	uniphier_sd_enable_irq(priv, UNIPHIER_SD_INFO1, UNIPHIER_SD_INFO1_MASK,
			       UNIPHIER_SD_INFO1_CMP, uniphier_sd_cmd_done,
			       false);
}

static void uniphier_sd_pio_read(struct uniphier_sd_priv *priv)
{
	unsigned int len = priv->cmd->data->blksz;
	int i;

	for (i = 0; i < len / 4; i++)
		*priv->pio_buf++ = readl(priv->regbase + UNIPHIER_SD_BUF);

	priv->pio_len -= len;

	if (!priv->pio_len)
		uniphier_sd_xfer_done(priv);
}

static void uniphier_sd_pio_write(struct uniphier_sd_priv *priv)
{
	unsigned int len = priv->cmd->data->blksz;
	int i;

	for (i = 0; i < len / 4; i++)
		writel(*priv->pio_buf++, priv->regbase + UNIPHIER_SD_BUF);

	priv->pio_len -= len;

	if (!priv->pio_len)
		uniphier_sd_xfer_done(priv);
}

static void uniphier_sd_pio_start(struct uniphier_sd_priv *priv,
				  struct mmc_data *data)
{
	u32 irq_bits;
	void (*handler)(struct uniphier_sd_priv *);

	BUG_ON(data->sg_len > 1);

	priv->pio_buf = sg_virt(data->sg);
	priv->pio_len = data->sg->length;

	BUG_ON(priv->pio_len % data->blksz);

	if (data->flags & MMC_DATA_READ) {
		irq_bits = UNIPHIER_SD_INFO2_BRE;
		handler = uniphier_sd_pio_read;
	} else {
		irq_bits = UNIPHIER_SD_INFO2_BWE;
		handler = uniphier_sd_pio_write;
	}

	uniphier_sd_enable_irq(priv, UNIPHIER_SD_INFO2, UNIPHIER_SD_INFO2_MASK,
			       irq_bits, handler, true);
}

static void uniphier_sd_dma_done(struct uniphier_sd_priv *priv)
{
	struct mmc_data *data = priv->cmd->data;

	dma_unmap_sg(mmc_dev(priv->host), data->sg, data->sg_len,
		     priv->dma_dir);

	uniphier_sd_xfer_done(priv);
}

static void uniphier_sd_dma_endisable(struct uniphier_sd_priv *priv,
				      int enable)
{
	u32 tmp;

	tmp = readl(priv->regbase + UNIPHIER_SD_EXTMODE);
	if (enable)
		tmp |= UNIPHIER_SD_EXTMODE_DMA_EN;
	else
		tmp &= ~UNIPHIER_SD_EXTMODE_DMA_EN;
	writel(tmp, priv->regbase + UNIPHIER_SD_EXTMODE);
}

static void uniphier_sd_dma_start(struct uniphier_sd_priv *priv,
				  struct mmc_data *data)
{
	dma_addr_t dma_addr;
	int sg_len;
	u32 irq_bits, tmp;

	BUG_ON(data->sg_len > 1);

	priv->dma_dir = data->flags & MMC_DATA_READ ? DMA_FROM_DEVICE :
						      DMA_TO_DEVICE;

	sg_len = dma_map_sg(mmc_dev(priv->host), data->sg, data->sg_len,
			    priv->dma_dir);
	if (!sg_len) {
		dev_warn(mmc_dev(priv->host),
			 "failed to dma_map.  falling back to PIO\n");
		uniphier_sd_pio_start(priv, data);
		return;
	}

	tmp = readl(priv->regbase + UNIPHIER_SD_DMA_MODE);

	if (data->flags & MMC_DATA_READ) {
		tmp |= UNIPHIER_SD_DMA_MODE_DIR_RD;
		irq_bits = UNIPHIER_SD_DMA_INFO1_END_RD2;
	} else {
		tmp &= ~UNIPHIER_SD_DMA_MODE_DIR_RD;
		irq_bits = UNIPHIER_SD_DMA_INFO1_END_WR;
	}

	writel(tmp, priv->regbase + UNIPHIER_SD_DMA_MODE);

	uniphier_sd_enable_irq(priv, UNIPHIER_SD_DMA_INFO1,
			       UNIPHIER_SD_DMA_INFO1_MASK, irq_bits,
			       uniphier_sd_dma_done, true);

	uniphier_sd_dma_endisable(priv, 1);

	dma_addr = sg_dma_address(data->sg);
	writel(dma_addr & U32_MAX, priv->regbase + UNIPHIER_SD_DMA_ADDR_L);
	writel((u64)dma_addr >> 32, priv->regbase + UNIPHIER_SD_DMA_ADDR_H);

	/* kick the DMA engine */
	writel(UNIPHIER_SD_DMA_CTL_START, priv->regbase + UNIPHIER_SD_DMA_CTL);
}

static void uniphier_sd_xfer_start(struct uniphier_sd_priv *priv,
				  struct mmc_data *data)
{
	if (priv->caps & UNIPHIER_SD_CAP_DMA_AVAIL)
		uniphier_sd_dma_start(priv, data);
	else
		uniphier_sd_pio_start(priv, data);
}

static void uniphier_sd_get_response(struct uniphier_sd_priv *priv,
				      struct mmc_command *cmd)
{
	if (!(mmc_resp_type(cmd) & MMC_RSP_PRESENT))
		return;

	if (mmc_resp_type(cmd) & MMC_RSP_136) {
		u32 bit_127_104 = readl(priv->regbase + UNIPHIER_SD_RSP76);
		u32 bit_103_72 = readl(priv->regbase + UNIPHIER_SD_RSP54);
		u32 bit_71_40 = readl(priv->regbase + UNIPHIER_SD_RSP32);
		u32 bit_39_8 = readl(priv->regbase + UNIPHIER_SD_RSP10);

		cmd->resp[0] = ((bit_127_104 & 0x00ffffff) << 8) |
			       ((bit_103_72  & 0xff000000) >> 24);
		cmd->resp[1] = ((bit_103_72  & 0x00ffffff) << 8) |
			       ((bit_71_40   & 0xff000000) >> 24);
		cmd->resp[2] = ((bit_71_40   & 0x00ffffff) << 8) |
			       ((bit_39_8    & 0xff000000) >> 24);
		cmd->resp[3] = (bit_39_8     & 0xffffff)   << 8;
	} else {
		/* bit 39-8 */
		cmd->resp[0] = readl(priv->regbase + UNIPHIER_SD_RSP10);
	}
}

static void uniphier_sd_handle_resp(struct uniphier_sd_priv *priv)
{
	struct mmc_command *cmd = priv->cmd;

	uniphier_sd_get_response(priv, cmd);

	if (cmd->data)
		uniphier_sd_xfer_start(priv, cmd->data);
	else
		uniphier_sd_cmd_done(priv);
}

static void uniphier_sd_send_cmd(struct uniphier_sd_priv *priv,
				 struct mmc_command *cmd)
{
	struct mmc_data *data = cmd->data;
	u32 tmp;

	priv->cmd = cmd;

	uniphier_sd_clear_all_irqs(priv);
	uniphier_sd_disable_all_irqs(priv);
	uniphier_sd_enable_err_irqs(priv);

	/* disable DMA once */
	uniphier_sd_dma_endisable(priv, 0);

	writel(cmd->arg, priv->regbase + UNIPHIER_SD_ARG);

	tmp = cmd->opcode;

	if (data) {
		writel(data->blksz, priv->regbase + UNIPHIER_SD_SIZE);
		writel(data->blocks, priv->regbase + UNIPHIER_SD_SECCNT);

		/* Do not send CMD12 automatically */
		tmp |= UNIPHIER_SD_CMD_NOSTOP | UNIPHIER_SD_CMD_DATA;

		if (data->blocks > 1)
			tmp |= UNIPHIER_SD_CMD_MULTI;

		if (data->flags & MMC_DATA_READ)
			tmp |= UNIPHIER_SD_CMD_RD;
	}

	/*
	 * Do not use the response type auto-detection on this hardware.
	 * CMD8, for example, has different response types on SD and eMMC,
	 * while this controller always assumes the response type for SD.
	 * Set the response type manually.
	 */
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		tmp |= UNIPHIER_SD_CMD_RSP_NONE;
		break;
	case MMC_RSP_R1:
		tmp |= UNIPHIER_SD_CMD_RSP_R1;
		break;
	case MMC_RSP_R1B:
		tmp |= UNIPHIER_SD_CMD_RSP_R1B;
		break;
	case MMC_RSP_R2:
		tmp |= UNIPHIER_SD_CMD_RSP_R2;
		break;
	case MMC_RSP_R3:
		tmp |= UNIPHIER_SD_CMD_RSP_R3;
		break;
	case 0x11:
		tmp |= UNIPHIER_SD_CMD_RSP_R1; /* FIX ME */
		break;
	default:
		WARN(1, "invalid response type %x\n", mmc_resp_type(cmd));
	}

	uniphier_sd_enable_irq(priv, UNIPHIER_SD_INFO1, UNIPHIER_SD_INFO1_MASK,
			       UNIPHIER_SD_INFO1_RSP, uniphier_sd_handle_resp,
			       true);

	dev_dbg(mmc_dev(priv->host), "send CMD%d (cmd = %x, arg = %x)\n",
		cmd->opcode, tmp, cmd->arg);

	writel(tmp, priv->regbase + UNIPHIER_SD_CMD);
}

static void uniphier_sd_cmd_done(struct uniphier_sd_priv *priv)
{
	struct mmc_command *cmd = priv->cmd;
	struct mmc_request *mrq = cmd->mrq;

	if (cmd == mrq->sbc && !cmd->error)
		uniphier_sd_send_cmd(priv, mrq->cmd);
	else if (mrq->stop && cmd == mrq->cmd && (!mrq->sbc || cmd->error))
		uniphier_sd_send_cmd(priv, mrq->stop);
	else
		uniphier_sd_request_done(priv);
}

static void uniphier_sd_request(struct mmc_host *host, struct mmc_request *mrq)
{
	struct uniphier_sd_priv *priv = mmc_priv(host);
	struct mmc_command *cmd = mrq->sbc ? mrq->sbc : mrq->cmd;

	dev_dbg(mmc_dev(host), "request CMD%d (sbc=%d, data=%d, stop=%d)\n",
		mrq->cmd->opcode, !!mrq->sbc, !!mrq->data, !!mrq->stop);

	if (readl(priv->regbase + UNIPHIER_SD_INFO2) & UNIPHIER_SD_INFO2_CBSY) {
		dev_err(mmc_dev(host), "command busy\n");
		mmc_request_done(host, mrq);
		cmd->error = -EBUSY;
		return;
	}

	uniphier_sd_send_cmd(priv, cmd);
}

static void uniphier_sd_set_bus_width(struct uniphier_sd_priv *priv,
				      struct mmc_ios *ios)
{
	u32 val, tmp;

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		val = UNIPHIER_SD_OPTION_WIDTH_1;
		break;
	case MMC_BUS_WIDTH_4:
		val = UNIPHIER_SD_OPTION_WIDTH_4;
		break;
	case MMC_BUS_WIDTH_8:
		val = UNIPHIER_SD_OPTION_WIDTH_8;
		break;
	default:
		BUG();
		break;
	}

	tmp = readl(priv->regbase + UNIPHIER_SD_OPTION);
	tmp &= ~UNIPHIER_SD_OPTION_WIDTH_MASK;
	tmp |= val;
	writel(tmp, priv->regbase + UNIPHIER_SD_OPTION);
}

static void uniphier_sd_set_ddr_mode(struct uniphier_sd_priv *priv,
				     struct mmc_ios *ios)
{
	u32 tmp;

	tmp = readl(priv->regbase + UNIPHIER_SD_IF_MODE);

	if (ios->timing == MMC_TIMING_UHS_DDR50 ||
	    ios->timing == MMC_TIMING_MMC_DDR52 ||
	    ios->timing == MMC_TIMING_MMC_HS400)
		tmp |= UNIPHIER_SD_IF_MODE_DDR;
	else
		tmp &= ~UNIPHIER_SD_IF_MODE_DDR;

	writel(tmp, priv->regbase + UNIPHIER_SD_IF_MODE);
}

static void uniphier_sd_set_clk_rate(struct uniphier_sd_priv *priv,
				     struct mmc_ios *ios)
{
	unsigned int divisor;
	u32 val, tmp;

	if (!ios->clock)
		return;

	divisor = DIV_ROUND_UP(priv->mclk, ios->clock);

	if (divisor <= 1)
		val = UNIPHIER_SD_CLKCTL_DIV1;
	else if (divisor <= 2)
		val = UNIPHIER_SD_CLKCTL_DIV2;
	else if (divisor <= 4)
		val = UNIPHIER_SD_CLKCTL_DIV4;
	else if (divisor <= 8)
		val = UNIPHIER_SD_CLKCTL_DIV8;
	else if (divisor <= 16)
		val = UNIPHIER_SD_CLKCTL_DIV16;
	else if (divisor <= 32)
		val = UNIPHIER_SD_CLKCTL_DIV32;
	else if (divisor <= 64)
		val = UNIPHIER_SD_CLKCTL_DIV64;
	else if (divisor <= 128)
		val = UNIPHIER_SD_CLKCTL_DIV128;
	else if (divisor <= 256)
		val = UNIPHIER_SD_CLKCTL_DIV256;
	else if (divisor <= 512)
		val = UNIPHIER_SD_CLKCTL_DIV512;
	else
		val = UNIPHIER_SD_CLKCTL_DIV1024;

	tmp = readl(priv->regbase + UNIPHIER_SD_CLKCTL);
	tmp &= ~UNIPHIER_SD_CLKCTL_DIV_MASK;
	tmp |= val | UNIPHIER_SD_CLKCTL_OFFEN | UNIPHIER_SD_CLKCTL_SCLKEN;
	writel(tmp, priv->regbase + UNIPHIER_SD_CLKCTL);
}

static void uniphier_sd_set_ios(struct mmc_host *host, struct mmc_ios *ios)
{
	struct uniphier_sd_priv *priv = mmc_priv(host);

	uniphier_sd_set_bus_width(priv, ios);
	uniphier_sd_set_ddr_mode(priv, ios);
	uniphier_sd_set_clk_rate(priv, ios);

	udelay(1000);
}

static int uniphier_sd_get_cd(struct mmc_host *host)
{
	struct uniphier_sd_priv *priv = mmc_priv(host);

	return !!(readl(priv->regbase + UNIPHIER_SD_INFO1) &
		  UNIPHIER_SD_INFO1_CD);
}

static int uniphier_sd_start_signal_voltage_switch(struct mmc_host *host,
						   struct mmc_ios *ios)
{
	struct uniphier_sd_priv *priv = mmc_priv(host);
	struct pinctrl_state *pinstate;
	u32 val;

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		if (!(priv->caps & UNIPHIER_SD_CAP_VDD_330))
			return -ENOTSUPP;
		val = UNIPHIER_SD_VOLT_330;
		pinstate = priv->pinstate_default;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		if (!(priv->caps & UNIPHIER_SD_CAP_VDD_180))
			return -ENOTSUPP;
		val = UNIPHIER_SD_VOLT_180;
		pinstate = priv->pinstate_1_8_v;
		break;
	default:
		return -ENOTSUPP;
	}

	if (priv->caps & UNIPHIER_SD_CAP_VOLTAGE_SWITCH) {
		u32 tmp;

		tmp = readl(priv->regbase + UNIPHIER_SD_VOLT);
		tmp &= ~UNIPHIER_SD_VOLT_MASK;
		tmp |= val;
		writel(tmp, priv->regbase + UNIPHIER_SD_VOLT);

		pinctrl_select_state(priv->pinctrl, pinstate);
	}

	return 0;
}

static void uniphier_sd_hw_reset(struct mmc_host *host)
{
	struct uniphier_sd_priv *priv = mmc_priv(host);

	WARN_ON(IS_ERR_OR_NULL(priv->clk_hw_reset));

	/* FIX ME */
	clk_disable_unprepare(priv->clk_hw_reset);
	cpu_relax();
	clk_prepare_enable(priv->clk_hw_reset);

	udelay(1);
}

static const struct mmc_host_ops uniphier_sd_ops = {
	.request = uniphier_sd_request,
	.set_ios = uniphier_sd_set_ios,
	.get_cd = uniphier_sd_get_cd,
	.start_signal_voltage_switch = uniphier_sd_start_signal_voltage_switch,
	.hw_reset = uniphier_sd_hw_reset,
};

static void uniphier_sd_host_init(struct uniphier_sd_priv *priv)
{
	u32 tmp;

	/* soft reset of the host */
	tmp = readl(priv->regbase + UNIPHIER_SD_SOFT_RST);
	tmp &= ~UNIPHIER_SD_SOFT_RST_RSTX;
	writel(tmp, priv->regbase + UNIPHIER_SD_SOFT_RST);
	tmp |= UNIPHIER_SD_SOFT_RST_RSTX;
	writel(tmp, priv->regbase + UNIPHIER_SD_SOFT_RST);

	/* use sector counter (UNIPHIER_SD_SECCNT) */
	writel(UNIPHIER_SD_STOP_SEC, priv->regbase + UNIPHIER_SD_STOP);

	/*
	 * Connected to 32bit AXI.
	 * This register dropped backward compatibility at version 0x10.
	 * Write an appropriate value depending on the IP version.
	 */
	writel(priv->version >= 0x10 ? 0x00000101 : 0x00000000,
	       priv->regbase + UNIPHIER_SD_HOST_MODE);

	if (priv->caps & UNIPHIER_SD_CAP_DMA_INTERNAL) {
		/* increment DMA address */
		tmp = readl(priv->regbase + UNIPHIER_SD_DMA_MODE);
		tmp |= UNIPHIER_SD_DMA_MODE_ADDR_INC;
		writel(tmp, priv->regbase + UNIPHIER_SD_DMA_MODE);
	}

	tmp = readl(priv->regbase + UNIPHIER_SD_INFO1_MASK);
	tmp &= ~UNIPHIER_SD_INFO1_INSERT;
	tmp &= ~UNIPHIER_SD_INFO1_REMOVE;
	writel(tmp, priv->regbase + UNIPHIER_SD_INFO1_MASK);
}

static int uniphier_sd_resource_init(struct platform_device *pdev,
				     struct uniphier_sd_priv *priv)
{
	struct device *dev = &pdev->dev;
	struct mmc_host *host = priv->host;
	struct resource *regs;
	int irq;
	int ret;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regbase = devm_ioremap_resource(dev, regs);
	if (IS_ERR(priv->regbase))
		return PTR_ERR(priv->regbase);

	priv->version = readl(priv->regbase + UNIPHIER_SD_VERSION) &
							UNIPHIER_SD_VERSION_IP;
	dev_info(dev, "version %x\n", priv->version);
	if (priv->version >= 0x10) {
		/* newer IP is equipped with DMA engine */
		priv->caps |= UNIPHIER_SD_CAP_DMA_AVAIL;
		priv->caps |= UNIPHIER_SD_CAP_DMA_INTERNAL;
	} else {
		/* try to use external DMA if available.  use PIO if not. */
		priv->dma_ext = dma_request_chan(dev, "rx-tx");
		if (!IS_ERR(priv->dma_ext))
			priv->caps |= UNIPHIER_SD_CAP_DMA_AVAIL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to get IRQ number");
		return irq;
	}

	ret = devm_request_threaded_irq(dev, irq, uniphier_sd_interrupt,
					uniphier_sd_interrupt_t, 0, pdev->name,
					priv);
	if (ret) {
		dev_err(dev, "failed to request irq %d\n", irq);
		return ret;
	}

	if (!of_property_read_bool(dev->of_node, "no-3-3-v"))
		priv->caps |= UNIPHIER_SD_CAP_VDD_330;

	if (!of_property_read_bool(dev->of_node, "no-1-8-v"))
		priv->caps |= UNIPHIER_SD_CAP_VDD_180;

	if (!(priv->caps & UNIPHIER_SD_CAP_VDD_330) &&
	    !(priv->caps & UNIPHIER_SD_CAP_VDD_180)) {
		dev_err(dev, "no operation voltage\n");
		return -EINVAL;
	}

	if (priv->caps & UNIPHIER_SD_CAP_VDD_330 &&
	    priv->caps & UNIPHIER_SD_CAP_VDD_180)
		priv->caps |= UNIPHIER_SD_CAP_VOLTAGE_SWITCH;

	priv->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(priv->pinctrl))
		return PTR_ERR(priv->pinctrl);

	priv->pinstate_default = pinctrl_lookup_state(priv->pinctrl,
						      PINCTRL_STATE_DEFAULT);
	if (IS_ERR(priv->pinstate_default)) {
		dev_err(dev, "no default pinctrl state\n");
		return PTR_ERR(priv->pinstate_default);
	}

	if (priv->caps & UNIPHIER_SD_CAP_VOLTAGE_SWITCH) {
		if (IS_ERR(priv->pinstate_1_8_v)) {
			priv->pinstate_1_8_v =
				pinctrl_lookup_state(priv->pinctrl, "1-8-v");
			dev_warn(dev, "failed to pinctrl for 1.8V signaling - disabling 1.8V mode\n");
			priv->caps &= ~UNIPHIER_SD_CAP_VDD_180;
			priv->caps &= ~UNIPHIER_SD_CAP_VOLTAGE_SWITCH;
		}
	}

	if (!(priv->caps & UNIPHIER_SD_CAP_VDD_180)) {
		host->caps &= ~MMC_CAP_UHS_SDR12;
		host->caps &= ~MMC_CAP_UHS_SDR25;
		host->caps &= ~MMC_CAP_UHS_SDR50;
		host->caps &= ~MMC_CAP_UHS_SDR104;
		host->caps &= ~MMC_CAP_UHS_DDR50;
	}

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	if (host->caps & MMC_CAP_HW_RESET) {
		priv->clk_hw_reset = devm_clk_get(dev, "hw-reset");
		if (IS_ERR(priv->clk_hw_reset)) {
			dev_warn(dev,
				 "faield to get hw_reset - disabling the capability.\n");
			host->caps &= ~MMC_CAP_HW_RESET;
		} else {
			clk_prepare_enable(priv->clk_hw_reset);
		}
	}

	/* use the highest clock rate */
	ret = clk_set_rate(priv->clk, ULONG_MAX);
	if (ret)
		return ret;

	priv->mclk = clk_get_rate(priv->clk);

	return 0;
}

static int uniphier_sd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_sd_priv *priv;
	struct mmc_host *host;
	int ret;

	host = mmc_alloc_host(sizeof(*priv), dev);
	if (!host)
		return -ENOMEM;

	priv = mmc_priv(host);
	priv->host = host;
	platform_set_drvdata(pdev, priv);

	ret = mmc_of_parse(host);
	if (ret)
		goto err;

	ret = uniphier_sd_resource_init(pdev, priv);
	if (ret)
		goto err;

	uniphier_sd_host_init(priv);

	host->ops = &uniphier_sd_ops;
	host->f_min = priv->mclk / 1024;
	host->f_max = priv->mclk;

	//host->max_segs = 0x7fff;  /* check later */
	host->max_blk_size = 512;	/* limitation by the buffer size */
	host->max_blk_count = U32_MAX; /* max value of UNIPHIER_SD_SECCNT */

	host->caps |= MMC_CAP_ERASE | MMC_CAP_CMD23;
	host->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	ret = mmc_add_host(host);

err:
	if (ret) {
		clk_disable_unprepare(priv->clk_hw_reset);
		clk_disable_unprepare(priv->clk);
		mmc_free_host(host);
	}

	return ret;
}

static int uniphier_sd_remove(struct platform_device *pdev)
{
	struct uniphier_sd_priv *priv = platform_get_drvdata(pdev);

	clk_disable_unprepare(priv->clk_hw_reset);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id uniphier_sd_match[] = {
	{ .compatible = "socionext,uniphier-sdhc" },
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
