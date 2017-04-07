/**
 * uniphier_thermal.c - Socionext UniPhier thermal management driver
 *
 * Copyright (c) 2014 Panasonic Corporation
 * Copyright (c) 2016 Socionext Inc.
 * All rights reserved.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

#include "thermal_core.h"

/* block */
#define PMPVTCTLEN				0xe000
#define PMPVTCTLEN_PMPVTCTLEN			BIT(0)
#define PMPVTCTLEN_PMPVTCTLEN_STOP		0
#define PMPVTCTLEN_PMPVTCTLEN_START		BIT(0)

#define PMPVTCTLMODE				0xe004
#define PMPVTCTLMODE_PMPVTCTLMODE		0xf
#define PMPVTCTLMODE_PMPVTCTLMODE_TEMPMON	0x5

#define EMONREPEAT				0xe040
#define EMONREPEAT_EMONENDLESS			BIT(24)
#define EMONREPEAT_EMONENDLESS_ENABLE		BIT(24)
#define EMONREPEAT_EMONPERIOD			0xf
#define EMONREPEAT_EMONPERIOD_1000000		0x9

/* common */
#define PMPVTCTLMODESEL				0xe900

#define SETALERT0				0xe910
#define SETALERT1				0xe914
#define SETALERT2				0xe918
#define SETALERT_EALERTTEMP0_OF			(0xff << 16)
#define SETALERT_EALERTEN0			BIT(0)
#define SETALERT_EALERTEN0_USE			BIT(0)

#define PMALERTINTCTL				0xe920
#define PMALERTINTCTL_ALERTINT_CLR(ch)		BIT(4 * (ch) + 2)
#define PMALERTINTCTL_ALERTINT_ST(ch)		BIT(4 * (ch) + 1)
#define PMALERTINTCTL_ALERTINT_EN(ch)		BIT(4 * (ch) + 0)
#define PMALERTINTCTL_ALL_BITS			0x777

#define TMOD					0xe928
#define TMOD_V_TMOD				0x1ff

#define TMODCOEF				0xee5c

/* SoC critical temperature is 95 degrees Celsius */
#define CRITICAL_TEMP_LIMIT			(95 * 1000)

/* Max # of alert channels */
#define ALERT_CH_NUM				3

/* SoC specific thermal sensor parameters */
struct uniphier_thermal_priv {
	unsigned int block_offset;
	unsigned int setup_address;
	u32 setup_value;
};

struct uniphier_thermal_dev {
	struct device *dev;
	struct regmap *regmap;
	bool alert_en[ALERT_CH_NUM];
	struct thermal_zone_device *tz_dev;
	const struct uniphier_thermal_priv *priv;
};

/* for UniPhier PXs2 */
static const struct uniphier_thermal_priv uniphier_thermal_priv_data_pxs2 = {
	.block_offset  = 0x000,
	.setup_address = 0x904,
	.setup_value   = 0x4f86e844,
};

/* for UniPhier LD20 */
static const struct uniphier_thermal_priv uniphier_thermal_priv_data_ld20 = {
	.block_offset  = 0x800,
	.setup_address = 0x938,
	.setup_value   = 0x4f22e8ee,
};

static int uniphier_thermal_initialize_sensor(struct uniphier_thermal_dev *tdev)
{
	int ret;
	struct regmap *regmap = tdev->regmap;
	unsigned int block_offset = tdev->priv->block_offset;
	unsigned int coef;

	/* stop PVT control */
	ret = regmap_write_bits(regmap, block_offset + PMPVTCTLEN,
				PMPVTCTLEN_PMPVTCTLEN, PMPVTCTLEN_PMPVTCTLEN_STOP);
	if (ret)
		return ret;

	/* set up default if missing eFuse */
	ret = regmap_read(regmap, TMODCOEF, &coef);
	if (ret)
		return ret;

	if (coef == 0) {
		ret = regmap_write(regmap, tdev->priv->setup_address,
				   tdev->priv->setup_value);
		if (ret)
			return ret;
	}

	/* set mode of temperature monitor */
	ret = regmap_write_bits(regmap, block_offset + PMPVTCTLMODE,
				PMPVTCTLMODE_PMPVTCTLMODE,
				PMPVTCTLMODE_PMPVTCTLMODE_TEMPMON);
	if (ret)
		return ret;

	/* set period (ENDLESS, 100ms) */
	ret = regmap_write_bits(regmap, block_offset + EMONREPEAT,
				EMONREPEAT_EMONENDLESS |
				EMONREPEAT_EMONPERIOD,
				EMONREPEAT_EMONENDLESS_ENABLE |
				EMONREPEAT_EMONPERIOD_1000000);
	if (ret)
		return ret;

	/* set mode select */
	ret = regmap_write(regmap, PMPVTCTLMODESEL, 0);
	if (ret)
		return ret;

	return 0;
}

static int uniphier_thermal_set_alert(struct uniphier_thermal_dev *tdev,
				      u32 ch, u32 temp)
{
	if (ch >= ALERT_CH_NUM)
		return -EINVAL;

	/* set alert temperature */
	return regmap_write_bits(tdev->regmap,
				 SETALERT0 + (ch << 2),
				 SETALERT_EALERTEN0 |
				 SETALERT_EALERTTEMP0_OF,
				 SETALERT_EALERTEN0_USE |
				 ((temp / 1000) << 16));
}

static void uniphier_thermal_enable_sensor(struct uniphier_thermal_dev *tdev)
{
	struct regmap *regmap = tdev->regmap;
	u32 bits = 0;
	int i;

	for (i = 0; i < ALERT_CH_NUM; i++)
		if (tdev->alert_en[i])
			bits |= PMALERTINTCTL_ALERTINT_EN(i);

	/* enable alert interrupt */
	regmap_write_bits(regmap, PMALERTINTCTL, PMALERTINTCTL_ALL_BITS, bits);

	/* start PVT control */
	regmap_write_bits(regmap, tdev->priv->block_offset + PMPVTCTLEN,
		    PMPVTCTLEN_PMPVTCTLEN, PMPVTCTLEN_PMPVTCTLEN_START);
}

static void uniphier_thermal_disable_sensor(struct uniphier_thermal_dev *tdev)
{
	struct regmap *regmap = tdev->regmap;

	/* disable alert interrupt */
	regmap_write_bits(regmap, PMALERTINTCTL, PMALERTINTCTL_ALL_BITS, 0);

	/* stop PVT control */
	regmap_write_bits(regmap, tdev->priv->block_offset + PMPVTCTLEN,
		    PMPVTCTLEN_PMPVTCTLEN, PMPVTCTLEN_PMPVTCTLEN_STOP);
}

static int uniphier_thermal_get_temp(void *data, int *out_temp)
{
	struct uniphier_thermal_dev *tdev = data;
	unsigned int temp;
	int ret;

	ret = regmap_read(tdev->regmap, TMOD, &temp);
	if (ret)
		return ret;

	*out_temp = (temp & TMOD_V_TMOD) * 1000;	/* millicelsius */

	return 0;
}

static const struct thermal_zone_of_device_ops uniphier_of_thermal_ops = {
	.get_temp = uniphier_thermal_get_temp,
};

static void uniphier_thermal_irq_clear(struct uniphier_thermal_dev *tdev)
{
	u32 mask = 0, bits = 0;
	int i;

	for (i = 0; i < ALERT_CH_NUM; i++) {
		mask |= (PMALERTINTCTL_ALERTINT_CLR(i) |
			 PMALERTINTCTL_ALERTINT_ST(i));
		bits |= PMALERTINTCTL_ALERTINT_CLR(i);
	}

	/* clear alert interrupt */
	regmap_write_bits(tdev->regmap, PMALERTINTCTL, mask, bits);
}

static irqreturn_t uniphier_thermal_alarm_handler(int irq, void *_tdev)
{
	struct uniphier_thermal_dev *tdev = _tdev;

	uniphier_thermal_irq_clear(tdev);

	thermal_zone_device_update(tdev->tz_dev, THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int uniphier_thermal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *parent;
	struct uniphier_thermal_dev *tdev;
	const struct thermal_trip *trips;
	int i, ret, irq, ntrips, crit_temp = INT_MAX;

	tdev = devm_kzalloc(dev, sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;

	tdev->dev = dev;
	tdev->priv = of_device_get_match_data(dev);
	if (WARN_ON(!tdev->priv))
		return -EINVAL;

	parent = of_get_parent(dev->of_node); /* parent should be syscon node */
	tdev->regmap = syscon_node_to_regmap(parent);
	of_node_put(parent);
	if (IS_ERR(tdev->regmap)) {
		dev_err(dev, "failed to get regmap\n");
		return PTR_ERR(tdev->regmap);
	}

	platform_set_drvdata(pdev, tdev);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/* register sensor */
	tdev->tz_dev = devm_thermal_zone_of_sensor_register(dev, 0, tdev,
						&uniphier_of_thermal_ops);
	if (IS_ERR(tdev->tz_dev))
		return PTR_ERR(tdev->tz_dev);

	/* get trip points */
	trips = of_thermal_get_trip_points(tdev->tz_dev);
	ntrips = of_thermal_get_ntrips(tdev->tz_dev);
	if (ntrips > ALERT_CH_NUM) {
		dev_err(dev, "thermal zone has too many trips.");
		return -E2BIG;
	}

	uniphier_thermal_initialize_sensor(tdev);
	for (i = 0; i < ntrips; i++) {
		if (trips[i].type == THERMAL_TRIP_CRITICAL &&
		    trips[i].temperature < crit_temp)
			crit_temp = trips[i].temperature;
		uniphier_thermal_set_alert(tdev, i, trips[i].temperature);
		tdev->alert_en[i] = true;
	}
	if (crit_temp > CRITICAL_TEMP_LIMIT) {
		dev_err(dev, "critical trip is over limit(>%d), or not set.",
			CRITICAL_TEMP_LIMIT);
		return -EINVAL;
	}

	ret = devm_request_irq(dev, irq, uniphier_thermal_alarm_handler,
			       0, "thermal", tdev);
	if (ret)
		return ret;

	/* enable sensor */
	uniphier_thermal_enable_sensor(tdev);

	return 0;
}

static int uniphier_thermal_remove(struct platform_device *pdev)
{
	struct uniphier_thermal_dev *tdev = platform_get_drvdata(pdev);

	/* disable sensor */
	uniphier_thermal_disable_sensor(tdev);

	return 0;
}

static const struct of_device_id uniphier_thermal_dt_ids[] = {
	{
		.compatible = "socionext,uniphier-pxs2-thermal",
		.data       = &uniphier_thermal_priv_data_pxs2,
	},
	{
		.compatible = "socionext,uniphier-ld20-thermal",
		.data       = &uniphier_thermal_priv_data_ld20,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_thermal_dt_ids);

static struct platform_driver uniphier_thermal_driver = {
	.probe = uniphier_thermal_probe,
	.remove = uniphier_thermal_remove,
	.driver = {
		.name = "uniphier-thermal",
		.of_match_table = uniphier_thermal_dt_ids,
	},
};
module_platform_driver(uniphier_thermal_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("UniPhier thermal management driver");
MODULE_LICENSE("GPL v2");
