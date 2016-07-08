/**
 * dwc3-uniphier.c - Socionext Uniphier DWC3 Specific Glue layer
 *
 * Copyright (c) 2015 Socionext Inc.
 *
 * Author:
 *	Kunihiko Hayashi <hayashi.kunihiko@socionext.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define NUM_U_MASK			(0x07)

#define UNIPHIER_USB_PORT_MAX		8

struct dwc3_uniphier_priv_t;

struct dwc3_uniphier {
	struct device		*dev;

	void __iomem		*base;
	struct clk		*u3clk[UNIPHIER_USB_PORT_MAX];
	struct clk		*u2clk[UNIPHIER_USB_PORT_MAX];
	bool			vbus_supply;
	u32			num_vbus;
	struct dwc3_uniphier_priv_t	*priv;
};

struct dwc3_uniphier_priv_t {

	void (*init)(struct dwc3_uniphier *);		/* initialize function */
	void (*exit)(struct dwc3_uniphier *);		/* finalize funtion */

	u32 reset_reg;			/* offset address for XHCI_RESET_CTL */
	u32 reset_bit_link;		/* bit number for XHCI_RESET_CTL.LINK_RESET_N */
	u32 reset_bit_iommu;		/* bit number for XHCI_RESET_CTL.IOMMU_RESET_N */

	u32 vbus_reg;			/* offset address for VBUS_CONTROL_REG */
	u32 vbus_bit_en;		/* bit number for VBUS_CONTROL_REG.DRVVBUS_REG_EN */
	u32 vbus_bit_onoff;		/* bit number for VBUS_CONTROL_REG.DRVVBUS_REG */

	u32 host_cfg_reg;		/* offset address for HOST_CONFIGO_REG */
	u32 host_cfg_bit_u2;		/* bit number for HOST_CONFIG0.NUM_U2 */
	u32 host_cfg_bit_u3;		/* bit number for HOST_CONFIG0.NUM_U3 */

	u32 u3phy_testi_reg;		/* offset address for TESTI_REG */
	u32 u3phy_testo_reg;		/* offset address for TESTO_REG */

	u32 u2phy_cfg0_reg;		/* offset address for P_U2PHY_CFG0 */
	u32 u2phy_cfg1_reg;		/* offset address for P_U2PHY_CFG1 */
};

#define NO_USE ((u32)~0)

static inline void maskwritel(void __iomem *base, u32 offset, u32 mask, u32 value)
{
	void __iomem *addr = base + offset;

	writel((readl(addr) & ~(mask)) | (value & mask), addr);
}

/* for PXs2 */

static void dwc3_uniphier_init_pxs2(struct dwc3_uniphier *);
static void dwc3_uniphier_exit_pxs2(struct dwc3_uniphier *);

static const struct dwc3_uniphier_priv_t dwc3_uniphier_priv_data_pxs2 = {
	.init = dwc3_uniphier_init_pxs2,
	.exit = dwc3_uniphier_exit_pxs2,
	.reset_reg        = 0x000,
	.reset_bit_link   = 15,
	.reset_bit_iommu  = NO_USE,
	.vbus_reg         = 0x100,
	.vbus_bit_en      = 3,
	.vbus_bit_onoff   = 4,
	.host_cfg_reg     = 0x400,
	.host_cfg_bit_u2  = 8,
	.host_cfg_bit_u3  = 11,
	.u3phy_testi_reg  = 0x300,
	.u3phy_testo_reg  = 0x304,
	.u2phy_cfg0_reg   = NO_USE,
	.u2phy_cfg1_reg   = NO_USE,
};

static inline void pphy_test_io(void __iomem *vptr_i, void __iomem *vptr_o,
				u32 data0, u32 mask1, u32 data1)
{
	u32 tmp_data;
	u32 rd;
	u32 wd;
	u32 ud;
	u32 ld;
	u32 test_i;
	u32 test_o;

	ud = 1;
	ld = data0;
	tmp_data = ((ud & 0xff) << 6) | ((ld & 0x1f) << 1);
	wd = tmp_data;
	writel(wd, vptr_i);
	readl(vptr_o);
	readl(vptr_o);

	rd = readl(vptr_o);
	ud = (rd & ~mask1) | data1;
	ld = data0;
	tmp_data = ((ud & 0xff) << 6) | ((ld & 0x1f) << 1);
	wd = tmp_data;
	writel(wd, vptr_i);
	readl(vptr_o);
	readl(vptr_o);

	wd = tmp_data | 1;
	writel(wd, vptr_i);
	readl(vptr_o);
	readl(vptr_o);

	wd = tmp_data;
	writel(wd, vptr_i);
	readl(vptr_o);
	readl(vptr_o);

	ud = 1;
	ld = data0;
	tmp_data = ((ud & 0xff) << 6) | ((ld & 0x1f) << 1);
	rd = readl(vptr_i);
	wd = rd | tmp_data;
	writel(wd, vptr_i);
	readl(vptr_o);
	readl(vptr_o);

	test_i = readl(vptr_i);
	test_o = readl(vptr_o);
}

static void dwc3_uniphier_init_pxs2(struct dwc3_uniphier *dwc3u)
{
	int i;
	void __iomem *vptr_i, *vptr_o;
	struct dwc3_uniphier_priv_t *priv = dwc3u->priv;
	int ss_instances;

	/* control the VBUS  */
	if (!dwc3u->vbus_supply){
		maskwritel(dwc3u->base, priv->vbus_reg,
			   (1 << priv->vbus_bit_en) | (1 << priv->vbus_bit_onoff),
			   (1 << priv->vbus_bit_en) | 0);
	}

	/* set up SS-PHY */
	ss_instances = (readl(dwc3u->base + priv->host_cfg_reg) >> priv->host_cfg_bit_u3) & NUM_U_MASK;
	for(i=0; i < ss_instances; i++) {
		vptr_i = dwc3u->base + priv->u3phy_testi_reg + (i * 0x10);
		vptr_o = dwc3u->base + priv->u3phy_testo_reg + (i * 0x10);

		pphy_test_io(vptr_i, vptr_o,  7, 0xf, 0xa);
		pphy_test_io(vptr_i, vptr_o,  8, 0xf, 0x3);
		pphy_test_io(vptr_i, vptr_o,  9, 0xf, 0x5);
		pphy_test_io(vptr_i, vptr_o, 11, 0xf, 0x9);
		pphy_test_io(vptr_i, vptr_o, 13, 0x60, 0x40);
		pphy_test_io(vptr_i, vptr_o, 27, 0x7, 0x7);
		pphy_test_io(vptr_i, vptr_o, 28, 0x3, 0x1);
	}

	/* release reset */
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link),
		   0);
	msleep(1);
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link),
		   (1 << priv->reset_bit_link));

	return;
}

static void dwc3_uniphier_exit_pxs2(struct dwc3_uniphier *dwc3u)
{
	struct dwc3_uniphier_priv_t *priv = dwc3u->priv;

	/* reset */
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link), 0);

	/* control the VBUS */
	if (!dwc3u->vbus_supply){
		maskwritel(dwc3u->base, priv->vbus_reg,
			   (1 << priv->vbus_bit_en), 0);
	}

	return;
}

/* for Pro5 */

static void dwc3_uniphier_init_pro5(struct dwc3_uniphier *);
static void dwc3_uniphier_exit_pro5(struct dwc3_uniphier *);

static const struct dwc3_uniphier_priv_t dwc3_uniphier_priv_data_pro5 = {
	.init = dwc3_uniphier_init_pro5,
	.exit = dwc3_uniphier_exit_pro5,
	.reset_reg        = 0x000,
	.reset_bit_link   = 15,
	.reset_bit_iommu  = 14,
	.vbus_reg         = 0x100,
	.vbus_bit_en      = 3,
	.vbus_bit_onoff   = 4,
	.host_cfg_reg     = 0x400,
	.host_cfg_bit_u2  = 8,
	.host_cfg_bit_u3  = 11,
	.u3phy_testi_reg  = NO_USE,
	.u3phy_testo_reg  = NO_USE,
	.u2phy_cfg0_reg   = NO_USE,
	.u2phy_cfg1_reg   = NO_USE,
};

#define XHCI_HSPHY_PARAM2_REG		(0x288)
#define XHCI_SSPHY_PARAM2_REG		(0x384)
#define XHCI_SSPHY_PARAM3_REG		(0x388)

static void dwc3_uniphier_init_pro5(struct dwc3_uniphier *dwc3u)
{
	struct dwc3_uniphier_priv_t *priv = dwc3u->priv;

	/* control the VBUS  */
	if (!dwc3u->vbus_supply){
		maskwritel(dwc3u->base, priv->vbus_reg,
			   (1 << priv->vbus_bit_en) | (1 << priv->vbus_bit_onoff),
			   (1 << priv->vbus_bit_en) | 0);
	}

	/* set up PHY */
	/* SSPHY Reference Clock Enable for SS function */
	maskwritel(dwc3u->base, XHCI_SSPHY_PARAM3_REG, 0x80000000, 0x80000000);
	/* HSPHY MPLL Frequency Multiplier Control, Frequency Select */
	maskwritel(dwc3u->base, XHCI_HSPHY_PARAM2_REG, 0x7f7f0000, 0x7d310000);
	/* SSPHY Loopback off */
	maskwritel(dwc3u->base, XHCI_SSPHY_PARAM2_REG, 0x00800000, 0x00000000);

	/* release PHY power on reset */
	maskwritel(dwc3u->base, priv->reset_reg, 0x00030000, 0x00000000);

	/* release reset */
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link) | (1 << priv->reset_bit_iommu),
		   0);
	msleep(1);
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link) | (1 << priv->reset_bit_iommu),
		   (1 << priv->reset_bit_link) | (1 << priv->reset_bit_iommu));

	return;
}

static void dwc3_uniphier_exit_pro5(struct dwc3_uniphier *dwc3u)
{
	struct dwc3_uniphier_priv_t *priv = dwc3u->priv;

	/* reset */
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link) | (1 << priv->reset_bit_iommu), 0);

	/* control the VBUS */
	if (!dwc3u->vbus_supply){
		maskwritel(dwc3u->base, priv->vbus_reg,
			   (1 << priv->vbus_bit_en), 0);
	}

	return;
}

/* for Pro4 */

static void dwc3_uniphier_init_pro4(struct dwc3_uniphier *);
static void dwc3_uniphier_exit_pro4(struct dwc3_uniphier *);

static const struct dwc3_uniphier_priv_t dwc3_uniphier_priv_data_pro4 = {
	.init = dwc3_uniphier_init_pro4,
	.exit = dwc3_uniphier_exit_pro4,
	.reset_reg        = 0x040,
	.reset_bit_link   = 4,
	.reset_bit_iommu  = 5,
	.vbus_reg         = 0x000,
	.vbus_bit_en      = 3,
	.vbus_bit_onoff   = 4,
	.host_cfg_reg     = NO_USE,
	.host_cfg_bit_u2  = NO_USE,
	.host_cfg_bit_u3  = NO_USE,
	.u3phy_testi_reg  = 0x010,
	.u3phy_testo_reg  = 0x014,
	.u2phy_cfg0_reg   = NO_USE,
	.u2phy_cfg1_reg   = NO_USE,
};

static inline void pphy_test_io_pro4(void __iomem *vptr_i, void __iomem *vptr_o, u32 data0)
{
	int i;

        writel(data0 | 0, vptr_i);
	for(i=0; i<10; i++){
		readl(vptr_o);
	}

        writel(data0 | 1, vptr_i);
	for(i=0; i<10; i++){
		readl(vptr_o);
	}

        writel(data0 | 0, vptr_i);
	for(i=0; i<10; i++){
		readl(vptr_o);
	}

	return;
}

static void dwc3_uniphier_init_pro4(struct dwc3_uniphier *dwc3u)
{
	void __iomem *vptr_i, *vptr_o;
	struct dwc3_uniphier_priv_t *priv = dwc3u->priv;

	/* control the VBUS  */
	if (!dwc3u->vbus_supply){
		maskwritel(dwc3u->base, priv->vbus_reg,
			   (1 << priv->vbus_bit_en) | (1 << priv->vbus_bit_onoff),
			   (1 << priv->vbus_bit_en) | 0);
	}

	/* set up SS-PHY */
	vptr_i = dwc3u->base + priv->u3phy_testi_reg;
	vptr_o = dwc3u->base + priv->u3phy_testo_reg;

	pphy_test_io_pro4(vptr_i, vptr_o, 0x206);
	pphy_test_io_pro4(vptr_i, vptr_o, 0x08e);
	pphy_test_io_pro4(vptr_i, vptr_o, 0x27c);
	pphy_test_io_pro4(vptr_i, vptr_o, 0x2b8);
	pphy_test_io_pro4(vptr_i, vptr_o, 0x102);
	pphy_test_io_pro4(vptr_i, vptr_o, 0x22a);
	pphy_test_io_pro4(vptr_i, vptr_o, 0x02c);
	pphy_test_io_pro4(vptr_i, vptr_o, 0x100);
	pphy_test_io_pro4(vptr_i, vptr_o, 0x09a);

	/* release reset */
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link) | (1 << priv->reset_bit_iommu),
		   0);
	msleep(1);
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link) | (1 << priv->reset_bit_iommu),
		   (1 << priv->reset_bit_link) | (1 << priv->reset_bit_iommu));

	return;
}

static void dwc3_uniphier_exit_pro4(struct dwc3_uniphier *dwc3u)
{
	struct dwc3_uniphier_priv_t *priv = dwc3u->priv;

	/* reset */
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link) | (1 << priv->reset_bit_iommu), 0);

	/* control the VBUS */
	if (!dwc3u->vbus_supply){
		maskwritel(dwc3u->base, priv->vbus_reg,
			   (1 << priv->vbus_bit_en), 0);
	}

	return;
}

/* for LD20 */

static void dwc3_uniphier_init_ld20(struct dwc3_uniphier *);
static void dwc3_uniphier_exit_ld20(struct dwc3_uniphier *);

static const struct dwc3_uniphier_priv_t dwc3_uniphier_priv_data_ld20 = {
	.init = dwc3_uniphier_init_ld20,
	.exit = dwc3_uniphier_exit_ld20,
	.reset_reg        = 0x000,
	.reset_bit_link   = 15,
	.reset_bit_iommu  = NO_USE,
	.vbus_reg         = 0x100,
	.vbus_bit_en      = 3,
	.vbus_bit_onoff   = 4,
	.host_cfg_reg     = 0x400,
	.host_cfg_bit_u2  = 8,
	.host_cfg_bit_u3  = 11,
	.u3phy_testi_reg  = 0x300,
	.u3phy_testo_reg  = 0x304,
	.u2phy_cfg0_reg   = 0x200,
	.u2phy_cfg1_reg   = 0x204,
};

#define EFUSE_BASE		0x5f900000
#define EFUSE_SIZE		0x0300
#define EFUSE_MON27_REG		0x0254
#define EFUSE_MON28_REG		0x0258
#define RSTCTL_REG		0x200C

static void ss_phy_setup_ld20(struct dwc3_uniphier *dwc3u, int ss_instances)
{
	/* No setup needed */
	return;
}

static void hs_phy_setup_ld20(struct dwc3_uniphier *dwc3u, int hs_instances)
{
	int i;

	void __iomem *efuse_addr;
	u32 efuse0, efuse1;
	u32 efuse_rterm_trim;
	u32 efuse_rtim_sel_t;
	u32 efuse_hs_i_trim;

	void __iomem *hs_phy_addr;
	u32 hs_phy_cfgl, hs_phy_cfgh;

	struct dwc3_uniphier_priv_t *priv = dwc3u->priv;

	/* read the customized phy parameter from efuse */
	efuse_addr = ioremap_nocache(EFUSE_BASE, EFUSE_SIZE);
	efuse0 = readl(efuse_addr + EFUSE_MON27_REG);
	efuse1 = readl(efuse_addr + EFUSE_MON28_REG);
	iounmap(efuse_addr);

	for (i=0; i < hs_instances; i++) {
		/* set the default recommended value */
		hs_phy_cfgl = 0x92306680;
		hs_phy_cfgh = 0x00000106;

		/* override by the value from efuse, if exist */
		if ((efuse0 | efuse1) != 0) {
			switch (i) {
			case 0:
				/* efuse0[5:4],[3:0],[19:16] */
				efuse_rterm_trim = (efuse0 & 0x00000030) >> 4;
				efuse_rtim_sel_t = (efuse0 & 0x0000000f);
				efuse_hs_i_trim  = (efuse0 & 0x000f0000) >> 16;
				break;
			case 1:
				/* efuse0[13:12],[11:8],[19:16] */
				efuse_rterm_trim = (efuse0 & 0x00003000) >> 12;
				efuse_rtim_sel_t = (efuse0 & 0x00000f00) >> 8;
				efuse_hs_i_trim  = (efuse0 & 0x000f0000) >> 16;
				break;
			case 2:
				/* efuse1[5:4],[3:0],[19:16] */
				efuse_rterm_trim = (efuse1 & 0x00000030) >> 4;
				efuse_rtim_sel_t = (efuse1 & 0x0000000f);
				efuse_hs_i_trim  = (efuse1 & 0x000f0000) >> 16;
				break;
			case 3:
				/* efuse1[13:12],[11:8],[19:16] */
				efuse_rterm_trim = (efuse1 & 0x00003000) >> 12;
				efuse_rtim_sel_t = (efuse1 & 0x00000f00) >>  8;
				efuse_hs_i_trim  = (efuse1 & 0x000f0000) >> 16;
				break;
			default:
				efuse_rterm_trim = 0;
				efuse_rtim_sel_t = 0;
				efuse_hs_i_trim  = 0;
				dev_err(dwc3u->dev,
					"illegal HS instances (%d)\n", i);
			}

			/* clear [31:28][15:12][7:6] and override them */
			hs_phy_cfgl = hs_phy_cfgl & 0x0fff0f3f;
			hs_phy_cfgl = hs_phy_cfgl
					| (efuse_rterm_trim << 6)
					| (efuse_rtim_sel_t << 12)
					| (efuse_hs_i_trim << 28);
		} else {
			/* change [27:26] 0x0->0x3 */
			hs_phy_cfgl = hs_phy_cfgl | (0x3 << 26);
		}

		/* write to the registers for the i-th HS instance */
		hs_phy_addr = dwc3u->base + priv->u2phy_cfg0_reg + (i * 0x10);
		writel(hs_phy_cfgl, hs_phy_addr);
		hs_phy_addr = dwc3u->base + priv->u2phy_cfg1_reg + (i * 0x10);
		writel(hs_phy_cfgh, hs_phy_addr);
	}

	return;
}

static void dwc3_uniphier_init_ld20(struct dwc3_uniphier *dwc3u)
{
	int i;

	struct device_node *clk_node;
	void __iomem *vptr;
	u32 rstctl_mask;

	struct dwc3_uniphier_priv_t *priv = dwc3u->priv;
	int hs_instances;
	int ss_instances;

	/* get the number of HS/SS port from the HW default value */
	hs_instances = (readl(dwc3u->base + priv->host_cfg_reg) >> priv->host_cfg_bit_u2) & NUM_U_MASK;
	ss_instances = (readl(dwc3u->base + priv->host_cfg_reg) >> priv->host_cfg_bit_u3) & NUM_U_MASK;

	/* number of VBUS */
	dwc3u->num_vbus = hs_instances;

	/* 2nd reset by SoC RSTCTL (do after reference clcck beocmes stable) */
	clk_node = of_parse_phandle(dwc3u->dev->of_node, "clocks", 0);
	if (clk_node) {
		vptr = of_iomap(clk_node, 0);
		if (vptr) {
			rstctl_mask = 0x0000f020;
			maskwritel(vptr, RSTCTL_REG,		/* issue the reset */
				   rstctl_mask, 0);
			msleep(1);
			maskwritel(vptr, RSTCTL_REG,		/* end the reset */
				   rstctl_mask, rstctl_mask);
			iounmap(vptr);
		} else {
			dev_warn(dwc3u->dev, "Failed to map clock register\n");
		}
		of_node_put(clk_node);
	} else {
		dev_warn(dwc3u->dev, "Failed to get clock select node\n");
	}

	/* control the VBUS */
	if (dwc3u->vbus_supply){
		for(i=0; i < dwc3u->num_vbus; i++) {
			/* enable the control and turn-on the VBUS */
			maskwritel(dwc3u->base, priv->vbus_reg + (i * 0x10),
				   (1 << priv->vbus_bit_en) | (1 << priv->vbus_bit_onoff),
				   (1 << priv->vbus_bit_en) | (1 << priv->vbus_bit_onoff));
		}
	}

	/* set up SS-PHY */
	ss_phy_setup_ld20(dwc3u, ss_instances);

	/* set up HS-PHY */
	hs_phy_setup_ld20(dwc3u, hs_instances);

	/* release reset by XHCI LINK RESET */
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link),
		   0);
	msleep(1);
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link),
		   (1 << priv->reset_bit_link));

	return;
}

static void dwc3_uniphier_exit_ld20(struct dwc3_uniphier *dwc3u)
{
	int i;
	struct dwc3_uniphier_priv_t *priv = dwc3u->priv;

	/* reset */
	maskwritel(dwc3u->base, priv->reset_reg,
		   (1 << priv->reset_bit_link), 0);

	/* control the VBUS */
	if (dwc3u->vbus_supply){
		for(i=0; i < dwc3u->num_vbus; i++) {
			/* disable the control */
			maskwritel(dwc3u->base, priv->vbus_reg + (i * 0x10),
				   (1 << priv->vbus_bit_en), 0);
		}
	}

	return;
}


static const struct of_device_id of_dwc3_match[];

static int dwc3_uniphier_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct dwc3_uniphier *dwc3u;
	struct resource	*res;
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id;
	void __iomem *base;
	int ret;
	int i;
	char clkname[8];

	of_id = of_match_device(of_dwc3_match, dev);
	if (!of_id)
		return -EINVAL;

	dwc3u = devm_kzalloc(&pdev->dev, sizeof(*dwc3u), GFP_KERNEL);
	if (!dwc3u)
		return -ENOMEM;

	platform_set_drvdata(pdev, dwc3u);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	dwc3u->dev         = dev;
	dwc3u->base        = base;
	dwc3u->vbus_supply = of_property_read_bool(node, "vbus-supply");
	dwc3u->priv        = (struct dwc3_uniphier_priv_t *)of_id->data;

	/* clk control */
	for(i=0; i<UNIPHIER_USB_PORT_MAX; i++) {

		/* usb3 clock */
		sprintf(clkname, "u3clk%d", i);
		dwc3u->u3clk[i] = devm_clk_get(dwc3u->dev, clkname);
		if (dwc3u->u3clk[i] == ERR_PTR(-ENOENT)) {
			dwc3u->u3clk[i] = NULL;
		}
		else if (IS_ERR(dwc3u->u3clk[i])) {
			dev_err(dwc3u->dev, "failed to get usb3 clock%d\n",i);
			return PTR_ERR(dwc3u->u3clk[i]);
		}

		/* usb2 clock */
		sprintf(clkname, "u2clk%d", i);
		dwc3u->u2clk[i] = devm_clk_get(dwc3u->dev, clkname);
		if (dwc3u->u2clk[i] == ERR_PTR(-ENOENT)) {
			dwc3u->u2clk[i] = NULL;
		}
		else if (IS_ERR(dwc3u->u2clk[i])) {
			dev_err(dwc3u->dev, "failed to get usb2 clock%d\n",i);
			return PTR_ERR(dwc3u->u2clk[i]);
		}
	}

	/* enable clk */
	for(i=0;i<UNIPHIER_USB_PORT_MAX;i++) {
		if (dwc3u->u3clk[i]) {
			ret = clk_prepare_enable(dwc3u->u3clk[i]);
			if (ret) {
				dev_err(dwc3u->dev, "failed to enable usb3 clock\n");
				goto err_clks;
			}
		}
		if (dwc3u->u2clk[i]) {
			ret = clk_prepare_enable(dwc3u->u2clk[i]);
			if (ret) {
				dev_err(dwc3u->dev, "failed to enable usb2 clock\n");
				goto err_clks;
			}
		}
	}

	/* initialize SoC glue */
	if (dwc3u->priv->init) {
		(dwc3u->priv->init)(dwc3u);
	}

	ret = of_platform_populate(node, NULL, NULL, dwc3u->dev);
	if (ret) {
		dev_err(dwc3u->dev, "failed to register core - %d\n", ret);
		goto err_dwc3;
	}

	return 0;

err_dwc3:
	if (dwc3u->priv->exit) {
		(dwc3u->priv->exit)(dwc3u);
	}

err_clks:
	/* disable clk */
	for(i=0;i<UNIPHIER_USB_PORT_MAX;i++) {
		if (dwc3u->u3clk[i])
			clk_disable_unprepare(dwc3u->u3clk[i]);
		if (dwc3u->u2clk[i])
			clk_disable_unprepare(dwc3u->u2clk[i]);
	}

	return ret;
}

static int dwc3_uniphier_remove(struct platform_device *pdev)
{
	struct dwc3_uniphier *dwc3u = platform_get_drvdata(pdev);
	int i;

	of_platform_depopulate(&pdev->dev);

	if (dwc3u->priv->exit) {
		(dwc3u->priv->exit)(dwc3u);
	}

	for(i=0;i<UNIPHIER_USB_PORT_MAX;i++) {
		if (dwc3u->u3clk[i]) {
			clk_disable_unprepare(dwc3u->u3clk[i]);
			devm_clk_put(dwc3u->dev, dwc3u->u3clk[i]);
		}
		if (dwc3u->u2clk[i]) {
			clk_disable_unprepare(dwc3u->u2clk[i]);
			devm_clk_put(dwc3u->dev, dwc3u->u2clk[i]);
		}
	}
	return 0;
}

static const struct of_device_id of_dwc3_match[] = {
	{
		.compatible = "socionext,proxstream2-dwc3",
		.data       = (void *)&dwc3_uniphier_priv_data_pxs2,
	},
	{
		.compatible = "socionext,ph1-pro5-dwc3",
		.data       = (void *)&dwc3_uniphier_priv_data_pro5,
	},
	{
		.compatible = "socionext,ph1-pro4-dwc3",
		.data       = (void *)&dwc3_uniphier_priv_data_pro4,
	},
	{
		.compatible = "socionext,ph1-ld20-dwc3",
		.data       = (void *)&dwc3_uniphier_priv_data_ld20,
	},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_dwc3_match);

static struct platform_driver dwc3_uniphier_driver = {
	.probe		= dwc3_uniphier_probe,
	.remove		= dwc3_uniphier_remove,
	.driver		= {
		.name	= "uniphier-dwc3",
		.of_match_table	= of_dwc3_match,
	},
};

module_platform_driver(dwc3_uniphier_driver);

MODULE_ALIAS("platform:uniphier-dwc3");
MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("DesignWare USB3 UniPhier Glue Layer");
MODULE_LICENSE("GPL v2");
