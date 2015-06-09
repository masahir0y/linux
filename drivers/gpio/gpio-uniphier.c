/*
 * Copyright (C) 2017 Socionext Inc.
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

#define UNIPHIER_GPIO_LINES_PER_BANK	8
#define UNIPHIER_GPIO_BANK_MASK		\
				GENMASK((UNIPHIER_GPIO_LINES_PER_BANK) - 1, 0)

#define UNIPHIER_GPIO_REG_DATA		0	/* data */
#define UNIPHIER_GPIO_REG_DIR		4	/* direction (1:in, 0:out) */

struct uniphier_gpio_priv {
	struct gpio_chip chip;
	void __iomem *regs;
	spinlock_t lock;
};

static unsigned int uniphier_gpio_bank_to_reg(unsigned int bank,
					      unsigned int reg)
{
	unsigned int reg_offset;

	reg_offset = (bank + 1) * 8 + reg;

	/*
	 * Unfortunately, there is a register hole at offset 0x90-0x9f.
	 * Add 0x10 when crossing the hole.
	 */
	if (reg_offset >= 0x90)
		reg_offset += 0x10;

	return reg_offset;
}

static void uniphier_gpio_get_bank_and_mask(unsigned int offset,
					    unsigned int *bank,
					    unsigned int *mask)
{
	*bank = offset / UNIPHIER_GPIO_LINES_PER_BANK;
	*mask = BIT(offset % UNIPHIER_GPIO_LINES_PER_BANK);
}

static void uniphier_gpio_bank_write(struct gpio_chip *chip,
				     unsigned int bank, unsigned int reg,
				     unsigned int mask, unsigned int value)
{
	struct uniphier_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned long flags;
	unsigned int reg_offset;
	u32 tmp;

	if (!mask)
		return;

	reg_offset = uniphier_gpio_bank_to_reg(bank, reg);

	spin_lock_irqsave(&priv->lock, flags);
	tmp = readl(priv->regs + reg_offset);
	tmp &= ~mask;
	tmp |= mask & value;
	writel(tmp, priv->regs + reg_offset);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void uniphier_gpio_offset_write(struct gpio_chip *chip, unsigned int offset,
				       unsigned int reg, int value)
{
	unsigned int bank, mask;

	uniphier_gpio_get_bank_and_mask(offset, &bank, &mask);

	uniphier_gpio_bank_write(chip, bank, reg, mask, value ? mask : 0);
}

static int uniphier_gpio_offset_read(struct gpio_chip *chip, unsigned int offset,
				     unsigned int reg)
{
	struct uniphier_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int bank, mask;
	unsigned int reg_offset;

	uniphier_gpio_get_bank_and_mask(offset, &bank, &mask);
	reg_offset = uniphier_gpio_bank_to_reg(bank, reg);

	return !!(readl(priv->regs + reg_offset) & mask);
}

static int uniphier_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void uniphier_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static int uniphier_gpio_get_direction(struct gpio_chip *chip,
				       unsigned int offset)
{
	return uniphier_gpio_offset_read(chip, offset, UNIPHIER_GPIO_REG_DIR);
}

static int uniphier_gpio_direction_input(struct gpio_chip *chip,
					 unsigned int offset)
{
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_REG_DIR, 1);

	return 0;
}

static int uniphier_gpio_direction_output(struct gpio_chip *chip,
					  unsigned int offset, int value)
{
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_REG_DATA, value);
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_REG_DIR, 0);

	return 0;
}

static int uniphier_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	return uniphier_gpio_offset_read(chip, offset, UNIPHIER_GPIO_REG_DATA);
}

static void uniphier_gpio_set(struct gpio_chip *chip,
			      unsigned int offset, int value)
{
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_REG_DATA, value);
}

static void uniphier_gpio_set_multiple(struct gpio_chip *chip,
				       unsigned long *mask, unsigned long *bits)
{
	unsigned int bank, shift, bank_mask, bank_bits;
	int i;

	for (i = 0; i < chip->ngpio; i += UNIPHIER_GPIO_LINES_PER_BANK) {
		bank = i / UNIPHIER_GPIO_LINES_PER_BANK;
		shift = i % BITS_PER_LONG;
		bank_mask = (mask[BIT_WORD(i)] >> shift) &
						UNIPHIER_GPIO_BANK_MASK;
		bank_bits = bits[BIT_WORD(i)] >> shift;

		uniphier_gpio_bank_write(chip, bank, UNIPHIER_GPIO_REG_DATA,
					 bank_mask, bank_bits);
	}
}

static int uniphier_gpio_of_xlate(struct gpio_chip *chip,
				  const struct of_phandle_args *gpiospec,
				  u32 *flags)
{
	if (WARN_ON(chip->of_gpio_n_cells != 3))
		return -EINVAL;

	if (WARN_ON(gpiospec->args_count != 3))
		return -EINVAL;

	/* args[0]: bank number */
	if (gpiospec->args[0] >= chip->ngpio / UNIPHIER_GPIO_LINES_PER_BANK)
		return -EINVAL;

	/* args[1]: line number in a bank */
	if (gpiospec->args[1] >= UNIPHIER_GPIO_LINES_PER_BANK)
		return -EINVAL;

	/* args[2]: flags */
	if (flags)
		*flags = gpiospec->args[2];

	return UNIPHIER_GPIO_LINES_PER_BANK * gpiospec->args[0] + gpiospec->args[1];
}

static int uniphier_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_gpio_priv *priv;
	struct gpio_chip *chip;
	struct resource *regs;
	u32 nbanks;
	int ret;

	ret = of_property_read_u32(dev->of_node, "gpio-banks", &nbanks);
	if (ret) {
		dev_err(dev, "failed to get gpio-banks property\n");
		return ret;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, regs);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	spin_lock_init(&priv->lock);

	chip = &priv->chip;

	chip->label = dev->of_node->full_name;
	chip->parent = dev;
	chip->request = uniphier_gpio_request;
	chip->free = uniphier_gpio_free;
	chip->get_direction = uniphier_gpio_get_direction;
	chip->direction_input = uniphier_gpio_direction_input;
	chip->direction_output = uniphier_gpio_direction_output;
	chip->get = uniphier_gpio_get;
	chip->set = uniphier_gpio_set;
	chip->set_multiple = uniphier_gpio_set_multiple;
	chip->base = -1;
	chip->ngpio = UNIPHIER_GPIO_LINES_PER_BANK * nbanks;
	chip->of_gpio_n_cells = 3;
	chip->of_xlate = uniphier_gpio_of_xlate;

	return devm_gpiochip_add_data(dev, chip, priv);
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
