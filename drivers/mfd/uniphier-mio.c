/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static int uniphier_mio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk *clk;
	int ret;

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, clk);

	return of_platform_default_populate(dev->of_node, NULL, dev);
}

static int uniphier_mio_remove(struct platform_device *pdev)
{
	struct clk *clk = platform_get_drvdata(pdev);

	clk_disable_unprepare(clk);

	return 0;
}

static const struct of_device_id uniphier_mio_match[] = {
	{ .compatible = "socionext,uniphier-sld3-mio" },
	{ .compatible = "socionext,uniphier-ld4-mio" },
	{ .compatible = "socionext,uniphier-pro4-mio" },
	{ .compatible = "socionext,uniphier-sld8-mio" },
	{ .compatible = "socionext,uniphier-ld11-mio" },
	{ .compatible = "socionext,uniphier-mio" },
	{ /* sentinel */ }
};

static struct platform_driver uniphier_mio_driver = {
	.probe = uniphier_mio_probe,
	.remove = uniphier_mio_remove,
	.driver = {
		.name = "uniphier-mio",
		.of_match_table = uniphier_mio_match,
	},
};
builtin_platform_driver(uniphier_mio_driver);
