// SPDX-License-Identifier: GPL-2.0
/*
 * NAND Flash Controller Device Driver for DT
 *
 * Copyright © 2011, Picochip.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "denali.h"

struct denali_dt {
<<<<<<< 3a62a98a96a25310f8b42c3d02b960bff921a484
	struct denali_nand_info	denali;
	struct clk *clk;	/* core clock */
	struct clk *clk_x;	/* bus interface clock */
	struct clk *clk_ecc;	/* ECC circuit clock */
=======
	struct denali_hw denali;
	struct clk *clk;
>>>>>>> mtd: nand: denali: decouple controller and NAND chips
};

struct denali_dt_data {
	unsigned int revision;
	unsigned int caps;
	const struct nand_ecc_caps *ecc_caps;
};

NAND_ECC_CAPS_SINGLE(denali_socfpga_ecc_caps, denali_calc_ecc_bytes,
		     512, 8, 15);
static const struct denali_dt_data denali_socfpga_data = {
	.caps = DENALI_CAP_HW_ECC_FIXUP,
	.ecc_caps = &denali_socfpga_ecc_caps,
};

NAND_ECC_CAPS_SINGLE(denali_uniphier_v5a_ecc_caps, denali_calc_ecc_bytes,
		     1024, 8, 16, 24);
static const struct denali_dt_data denali_uniphier_v5a_data = {
	.caps = DENALI_CAP_HW_ECC_FIXUP |
		DENALI_CAP_DMA_64BIT,
	.ecc_caps = &denali_uniphier_v5a_ecc_caps,
};

NAND_ECC_CAPS_SINGLE(denali_uniphier_v5b_ecc_caps, denali_calc_ecc_bytes,
		     1024, 8, 16);
static const struct denali_dt_data denali_uniphier_v5b_data = {
	.revision = 0x0501,
	.caps = DENALI_CAP_HW_ECC_FIXUP |
		DENALI_CAP_DMA_64BIT,
	.ecc_caps = &denali_uniphier_v5b_ecc_caps,
};

static const struct of_device_id denali_nand_dt_ids[] = {
	{
		.compatible = "altr,socfpga-denali-nand",
		.data = &denali_socfpga_data,
	},
	{
		.compatible = "socionext,uniphier-denali-nand-v5a",
		.data = &denali_uniphier_v5a_data,
	},
	{
		.compatible = "socionext,uniphier-denali-nand-v5b",
		.data = &denali_uniphier_v5b_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, denali_nand_dt_ids);

static int denali_dt_chip_init(struct denali_hw *denali,
			       struct device_node *chip_np)
{
	struct denali_chip *denali_chip;
	u32 bank;
	int nbanks, i, ret;

	nbanks = of_property_count_u32_elems(chip_np, "reg");
	if (nbanks < 0)
		return nbanks;

	denali_chip = devm_kzalloc(denali->dev,
				   sizeof(*denali_chip) +
				   nbanks * sizeof(denali_chip->banks[0]),
				   GFP_KERNEL);
	if (!denali_chip)
		return -ENOMEM;

	for (i = 0; i < nbanks; i++) {
		ret = of_property_read_u32_index(chip_np, "reg", i, &bank);
		if (ret)
			return ret;

		if (bank >= DENALI_NR_BANKS) {
			return -EINVAL;
		}

		/* Does a chip really exist there? */
		if (!(denali->probed_banks & BIT(bank)))
			break;

		denali_chip->banks[i] = bank;
	}

	/* number of chip selects that are really connected to chips */
	denali_chip->nbanks = i;

	return denali_chip_init(denali, denali_chip);
}

static int denali_dt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct denali_dt *dt;
	const struct denali_dt_data *data;
	struct denali_hw *denali;
	struct device_node *np;
	int i, ret;

	dt = devm_kzalloc(dev, sizeof(*dt), GFP_KERNEL);
	if (!dt)
		return -ENOMEM;
	denali = &dt->denali;

	data = of_device_get_match_data(dev);
	if (data) {
		denali->revision = data->revision;
		denali->caps = data->caps;
		denali->ecc_caps = data->ecc_caps;
	}

	denali->dev = dev;
	denali->irq = platform_get_irq(pdev, 0);
	if (denali->irq < 0) {
		dev_err(dev, "no irq defined\n");
		return denali->irq;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "denali_reg");
	denali->reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(denali->reg))
		return PTR_ERR(denali->reg);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "nand_data");
	denali->host = devm_ioremap_resource(dev, res);
	if (IS_ERR(denali->host))
		return PTR_ERR(denali->host);

<<<<<<< 3a62a98a96a25310f8b42c3d02b960bff921a484
	/*
	 * A single anonymous clock is supported for the backward compatibility.
	 * New platforms should support all the named clocks.
	 */
	dt->clk = devm_clk_get(dev, "nand");
	if (IS_ERR(dt->clk))
		dt->clk = devm_clk_get(dev, NULL);
=======
	dt->clk = devm_clk_get(dev, NULL);
>>>>>>> mtd: nand: denali: decouple controller and NAND chips
	if (IS_ERR(dt->clk)) {
		dev_err(dev, "no clk available\n");
		return PTR_ERR(dt->clk);
	}

	dt->clk_x = devm_clk_get(dev, "nand_x");
	if (IS_ERR(dt->clk_x))
		dt->clk_x = NULL;

	dt->clk_ecc = devm_clk_get(dev, "ecc");
	if (IS_ERR(dt->clk_ecc))
		dt->clk_ecc = NULL;

	ret = clk_prepare_enable(dt->clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dt->clk_x);
	if (ret)
		goto out_disable_clk;

	ret = clk_prepare_enable(dt->clk_ecc);
	if (ret)
		goto out_disable_clk_x;

	if (dt->clk_x) {
		denali->clk_rate = clk_get_rate(dt->clk);
		denali->clk_x_rate = clk_get_rate(dt->clk_x);
	} else {
		/*
		 * Hardcode the clock rates for the backward compatibility.
		 * This works for both SOCFPGA and UniPhier.
		 */
		dev_notice(dev,
			   "necessary clock is missing. default clock rates are used.\n");
		denali->clk_rate = 50000000;
		denali->clk_x_rate = 200000000;
	}

	ret = denali_init(denali);
	if (ret)
		goto out_disable_clk_ecc;

	i = 0;
	for_each_child_of_node(dev->of_node, np) {
		ret = denali_dt_chip_init(denali, np);
		if (ret)
			dev_warn(dev, "Failed to init chip %d (error %d)\n",
				 i, ret);
		/* even if it failed, continue to init the next chip */
		i++;
	}

	platform_set_drvdata(pdev, dt);

	return 0;

out_disable_clk_ecc:
	clk_disable_unprepare(dt->clk_ecc);
out_disable_clk_x:
	clk_disable_unprepare(dt->clk_x);
out_disable_clk:
	clk_disable_unprepare(dt->clk);

	return ret;
}

static int denali_dt_remove(struct platform_device *pdev)
{
	struct denali_dt *dt = platform_get_drvdata(pdev);

	denali_remove(&dt->denali);
	clk_disable_unprepare(dt->clk_ecc);
	clk_disable_unprepare(dt->clk_x);
	clk_disable_unprepare(dt->clk);

	return 0;
}

static struct platform_driver denali_dt_driver = {
	.probe		= denali_dt_probe,
	.remove		= denali_dt_remove,
	.driver		= {
		.name	= "denali-nand-dt",
		.of_match_table	= denali_nand_dt_ids,
	},
};
module_platform_driver(denali_dt_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("DT driver for Denali NAND controller");
