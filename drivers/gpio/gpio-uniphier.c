/*
 * Copyright (C) 2015 Socionext Inc.
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

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define UNIPHIER_GPIO_NR_PORTS		8

#define UNIPHIER_GPIO_REG_DATA		0	/* data */
#define UNIPHIER_GPIO_REG_DIR		4	/* direction (1:in, 0:out) */

struct uniphier_gpio_priv {
	struct gpio_chip chip;
	void __iomem *regs;
	spinlock_t lock;
};

static void uniphier_gpio_bank_write(struct gpio_chip *chip, unsigned reg,
				     unsigned mask, unsigned value)
{
	struct uniphier_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&priv->lock, flags);
	tmp = readl(priv->regs + reg);
	tmp &= ~mask;
	tmp |= mask & value;
	writel(tmp, priv->regs + reg);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void uniphier_gpio_offset_write(struct gpio_chip *chip, unsigned reg,
				       unsigned offset, int value)
{
	uniphier_gpio_bank_write(chip, reg, BIT(offset), value << offset);
}

static int uniphier_gpio_offset_read(struct gpio_chip *chip, unsigned reg,
				     unsigned offset)
{
	struct uniphier_gpio_priv *priv = gpiochip_get_data(chip);

	return !!(readl(priv->regs + reg) & BIT(offset));
}

static int uniphier_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void uniphier_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static int uniphier_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	return uniphier_gpio_offset_read(chip, UNIPHIER_GPIO_REG_DIR, offset);
}

static int uniphier_gpio_direction_input(struct gpio_chip *chip,
					 unsigned offset)
{
	uniphier_gpio_offset_write(chip, UNIPHIER_GPIO_REG_DIR, offset, 1);

	return 0;
}

static int uniphier_gpio_direction_output(struct gpio_chip *chip,
					  unsigned offset, int value)
{
	uniphier_gpio_offset_write(chip, UNIPHIER_GPIO_REG_DATA, offset, value);
	uniphier_gpio_offset_write(chip, UNIPHIER_GPIO_REG_DIR, offset, 0);

	return 0;
}

static int uniphier_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return uniphier_gpio_offset_read(chip, UNIPHIER_GPIO_REG_DATA, offset);
}

static void uniphier_gpio_set(struct gpio_chip *chip,
			      unsigned offset, int value)
{
	uniphier_gpio_offset_write(chip, UNIPHIER_GPIO_REG_DATA, offset, value);
}

static void uniphier_gpio_set_multiple(struct gpio_chip *chip,
				       unsigned long *mask,
				       unsigned long *bits)
{
	unsigned bank_mask = GENMASK(UNIPHIER_GPIO_NR_PORTS - 1, 0);

	uniphier_gpio_bank_write(chip, UNIPHIER_GPIO_REG_DATA,
				 *mask & bank_mask, *bits & bank_mask);
}

static int uniphier_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_gpio_priv *priv;
	struct resource *regs;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, regs);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	spin_lock_init(&priv->lock);

	priv->chip.label = dev->of_node->full_name;
	priv->chip.parent = dev;
	priv->chip.request = uniphier_gpio_request;
	priv->chip.free = uniphier_gpio_free;
	priv->chip.get_direction = uniphier_gpio_get_direction;
	priv->chip.direction_input = uniphier_gpio_direction_input;
	priv->chip.direction_output = uniphier_gpio_direction_output;
	priv->chip.get = uniphier_gpio_get;
	priv->chip.set = uniphier_gpio_set;
	priv->chip.set_multiple = uniphier_gpio_set_multiple;
	priv->chip.base = -1;
	priv->chip.ngpio = UNIPHIER_GPIO_NR_PORTS;

	return devm_gpiochip_add_data(dev, &priv->chip, priv);
}

static const struct of_device_id uniphier_gpio_match[] = {
	{ .compatible = "socionext,uniphier-gpio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_gpio_match);

static struct platform_driver uniphier_gpio_driver = {
	.probe = uniphier_gpio_probe,
	.driver = {
		.name = "uniphier-gpio",
		.of_match_table = uniphier_gpio_match,
	},
};
module_platform_driver(uniphier_gpio_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier GPIO driver");
MODULE_LICENSE("GPL");
