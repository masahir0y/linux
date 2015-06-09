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
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define UNIPHIER_GPIO_NR_PORTS		8
#define UNIPHIER_GPIO_BANK_MASK		\
				((1UL << (UNIPHIER_GPIO_PORTS_PER_BANK)) - 1)

#define UNIPHIER_GPIO_REG_DATA		0	/* data */
#define UNIPHIER_GPIO_REG_DIR		4	/* direction (1:in, 0:out) */

struct uniphier_gpio_priv {
	struct of_mm_gpio_chip mmchip;
	spinlock_t lock;
};

static void uniphier_gpio_offset_write(struct gpio_chip *chip, unsigned offset,
				       unsigned reg, int value)
{
	struct of_mm_gpio_chip *mmchip = to_of_mm_gpio_chip(chip);
	struct uniphier_gpio_priv *priv;
	unsigned long flags;
	u32 mask = BIT(offset);
	u32 tmp;

	priv = container_of(mmchip, struct uniphier_gpio_priv, mmchip);

	spin_lock_irqsave(&priv->lock, flags);
	tmp = readl(mmchip->regs + reg);
	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	writel(tmp, mmchip->regs + reg);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int uniphier_gpio_offset_read(struct gpio_chip *chip, unsigned offset,
				     unsigned reg)
{
	struct of_mm_gpio_chip *mmchip = to_of_mm_gpio_chip(chip);

	return readl(mmchip->regs + reg) & BIT(offset) ? 1 : 0;
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
	return uniphier_gpio_offset_read(chip, offset, UNIPHIER_GPIO_REG_DIR) ?
						GPIOF_DIR_IN : GPIOF_DIR_OUT;
}

static int uniphier_gpio_direction_input(struct gpio_chip *chip,
					 unsigned offset)
{
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_REG_DIR, 1);

	return 0;
}

static int uniphier_gpio_direction_output(struct gpio_chip *chip,
					  unsigned offset, int value)
{
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_REG_DATA, value);
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_REG_DIR, 0);

	return 0;
}

static int uniphier_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return uniphier_gpio_offset_read(chip, offset, UNIPHIER_GPIO_REG_DATA);
}

static void uniphier_gpio_set(struct gpio_chip *chip,
			      unsigned offset, int value)
{
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_REG_DATA, value);
}
#if 0
static void uniphier_gpio_set_multiple(struct gpio_chip *chip,
				       unsigned long *mask,
				       unsigned long *bits)
{
	unsigned bank, shift, bank_mask, bank_bits;
	int i;

	for (i = 0; i < chip->ngpio; i += UNIPHIER_GPIO_PORTS_PER_BANK) {
		bank = i / UNIPHIER_GPIO_PORTS_PER_BANK;
		shift = i % BITS_PER_LONG;
		bank_mask = (mask[BIT_WORD(i)] >> shift) &
						UNIPHIER_GPIO_BANK_MASK;
		bank_bits = bits[BIT_WORD(i)] >> shift;

		uniphier_gpio_bank_write(chip, bank, UNIPHIER_GPIO_REG_DATA,
					 bank_mask, bank_bits);
	}
}
#endif
static int uniphier_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_gpio_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);

	priv->mmchip.gc.parent = dev;
	priv->mmchip.gc.request = uniphier_gpio_request;
	priv->mmchip.gc.free = uniphier_gpio_free;
	priv->mmchip.gc.get_direction = uniphier_gpio_get_direction;
	priv->mmchip.gc.direction_input = uniphier_gpio_direction_input;
	priv->mmchip.gc.direction_output = uniphier_gpio_direction_output;
	priv->mmchip.gc.get = uniphier_gpio_get;
	priv->mmchip.gc.set = uniphier_gpio_set;
	//priv->mmchip.gc.set_multiple = uniphier_gpio_set_multiple;
	priv->mmchip.gc.ngpio = UNIPHIER_GPIO_NR_PORTS;

	ret = of_mm_gpiochip_add(dev->of_node, &priv->mmchip);
	if (ret) {
		dev_err(dev, "failed to add memory mapped gpiochip\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int uniphier_gpio_remove(struct platform_device *pdev)
{
	struct uniphier_gpio_priv *priv = platform_get_drvdata(pdev);

	of_mm_gpiochip_remove(&priv->mmchip);

	return 0;
}

static const struct of_device_id uniphier_gpio_match[] = {
	{ .compatible = "socionext,uniphier-gpio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_gpio_match);

static struct platform_driver uniphier_gpio_driver = {
	.probe = uniphier_gpio_probe,
	.remove = uniphier_gpio_remove,
	.driver = {
		.name = "uniphier-gpio",
		.of_match_table = uniphier_gpio_match,
	},
};
module_platform_driver(uniphier_gpio_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier GPIO driver");
MODULE_LICENSE("GPL");
