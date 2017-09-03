/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2015-2017 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#ifndef __PINCTRL_UNIPHIER_H__
#define __PINCTRL_UNIPHIER_H__

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/build_bug.h>
#include <linux/kernel.h>
#include <linux/types.h>

struct platform_device;

#define UNIPHIER_PIN_IECTRL_MASK	GENMASK(2, 0) /* input enable ctrl */
#define UNIPHIER_PIN_DRVCTRL_MASK	GENMASK(11, 3) /* drive strength ctrl */
#define UNIPHIER_PIN_DRV_TYPE_MASK	GENMASK(14, 12) /* drive control type */
#define UNIPHIER_PIN_PUPDCTRL_MASK	GENMASK(23, 15) /* pull-up/down ctrl */
#define UNIPHIER_PIN_PULL_DIR_MASK	GENMASK(26, 24) /* pull direction */

#define UNIPHIER_PIN_IECTRL_NONE	(UNIPHIER_PIN_IECTRL_MASK)
#define UNIPHIER_PIN_IECTRL_EXIST	0

/* drive control type */
enum uniphier_pin_drv_type {
	UNIPHIER_PIN_DRV_1BIT,		/* 2 level control: 4/8 mA */
	UNIPHIER_PIN_DRV_2BIT,		/* 4 level control: 8/12/16/20 mA */
	UNIPHIER_PIN_DRV_3BIT,		/* 8 level control: 4/5/7/9/11/12/14/16 mA */
	UNIPHIER_PIN_DRV_FIXED4,	/* fixed to 4mA */
	UNIPHIER_PIN_DRV_FIXED5,	/* fixed to 5mA */
	UNIPHIER_PIN_DRV_FIXED8,	/* fixed to 8mA */
	UNIPHIER_PIN_DRV_NONE,		/* no support (input only pin) */
};

/* direction of pull register (no pin supports bi-directional pull biasing) */
enum uniphier_pin_pull_dir {
	UNIPHIER_PIN_PULL_UP,		/* pull-up or disabled */
	UNIPHIER_PIN_PULL_DOWN,		/* pull-down or disabled */
	UNIPHIER_PIN_PULL_UP_FIXED,	/* always pull-up */
	UNIPHIER_PIN_PULL_DOWN_FIXED,	/* always pull-down */
	UNIPHIER_PIN_PULL_NONE,		/* no pull register */
};

#define UNIPHIER_PIN_IECTRL(x)	FIELD_PREP((UNIPHIER_PIN_IECTRL_MASK), (x))
#define UNIPHIER_PIN_DRVCTRL(x)	FIELD_PREP((UNIPHIER_PIN_DRVCTRL_MASK), (x))
#define UNIPHIER_PIN_DRV_TYPE(x)	FIELD_PREP((UNIPHIER_PIN_DRV_TYPE_MASK), (x))
#define UNIPHIER_PIN_PUPDCTRL(x)	FIELD_PREP((UNIPHIER_PIN_PUPDCTRL_MASK), (x))
#define UNIPHIER_PIN_PULL_DIR(x)	FIELD_PREP((UNIPHIER_PIN_PULL_DIR_MASK), (x))

#define UNIPHIER_PIN_ATTR_PACKED(iectrl, drvctrl, drv_type, pupdctrl, pull_dir)\
				(UNIPHIER_PIN_IECTRL(iectrl) |		\
				 UNIPHIER_PIN_DRVCTRL(drvctrl) |	\
				 UNIPHIER_PIN_DRV_TYPE(drv_type) |	\
				 UNIPHIER_PIN_PUPDCTRL(pupdctrl) |	\
				 UNIPHIER_PIN_PULL_DIR(pull_dir))

struct uniphier_pinctrl_group {
	const char *name;
	const unsigned *pins;
	unsigned num_pins;
	const int *muxvals;
};

struct uniphier_pinmux_function {
	const char *name;
	const char * const *groups;
	unsigned num_groups;
};

struct uniphier_pinctrl_socdata {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct uniphier_pinctrl_group *groups;
	int groups_count;
	const struct uniphier_pinmux_function *functions;
	int functions_count;
	int (*get_gpio_muxval)(unsigned int pin, unsigned int gpio_offset);
	unsigned int caps;
#define UNIPHIER_PINCTRL_CAPS_PERPIN_IECTRL	BIT(1)
#define UNIPHIER_PINCTRL_CAPS_DBGMUX_SEPARATE	BIT(0)
};

#define UNIPHIER_PINCTRL_PIN(a, b, c, d, e, f, g)			\
{									\
	.number = a,							\
	.name = b,							\
	.drv_data = (void *)UNIPHIER_PIN_ATTR_PACKED(c, d, e, f, g),	\
}

#define __UNIPHIER_PINCTRL_GROUP(grp, mux)				\
	{								\
		.name = #grp,						\
		.pins = grp##_pins,					\
		.num_pins = ARRAY_SIZE(grp##_pins),			\
		.muxvals = mux,						\
	}

#define UNIPHIER_PINCTRL_GROUP(grp)					\
	__UNIPHIER_PINCTRL_GROUP(grp,					\
			grp##_muxvals +					\
			BUILD_BUG_ON_ZERO(ARRAY_SIZE(grp##_pins) !=	\
					  ARRAY_SIZE(grp##_muxvals)))

#define UNIPHIER_PINCTRL_GROUP_GPIO(grp)				\
	__UNIPHIER_PINCTRL_GROUP(grp, NULL)

#define UNIPHIER_PINMUX_FUNCTION(func)					\
	{								\
		.name = #func,						\
		.groups = func##_groups,				\
		.num_groups = ARRAY_SIZE(func##_groups),		\
	}

int uniphier_pinctrl_probe(struct platform_device *pdev,
			   const struct uniphier_pinctrl_socdata *socdata);

extern const struct dev_pm_ops uniphier_pinctrl_pm_ops;

#endif /* __PINCTRL_UNIPHIER_H__ */
