/*
 * Copyright 2015 IBM Corp.
 *
 * Joel Stanley <joel@jms.id.au>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>

struct aspeed_gpio {
	struct gpio_chip chip;
	spinlock_t lock;
	void __iomem *base; 
};

#define GPIO_DATA	0x00
#define GPIO_DIR	0x04

static inline struct aspeed_gpio *to_aspeed_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct aspeed_gpio, chip);
}

static int aspeed_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_gpio *gpio = to_aspeed_gpio(gc); 

	return !!(ioread32(gpio->base + GPIO_DATA) & BIT(offset));
}

static void aspeed_gpio_set(struct gpio_chip *gc, unsigned int offset,
			    int val)
{
	struct aspeed_gpio *gpio = to_aspeed_gpio(gc); 
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&gpio->lock, flags);

	reg = ioread32(gpio->base + GPIO_DATA);
	if (val)
		reg |= BIT(offset);
	else
		reg &= ~BIT(offset);

	iowrite32(reg, gpio->base + GPIO_DATA);

	spin_unlock_irqrestore(&gpio->lock, flags);
}

static int aspeed_gpio_dir_in(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_gpio *gpio = to_aspeed_gpio(gc); 
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&gpio->lock, flags);

	reg = ioread32(gpio->base + GPIO_DIR);
	iowrite32(reg | BIT(offset), gpio->base + GPIO_DIR);

	spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int aspeed_gpio_dir_out(struct gpio_chip *gc,
			       unsigned int offset, int val)
{
	struct aspeed_gpio *gpio = to_aspeed_gpio(gc); 
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&gpio->lock, flags);

	reg = ioread32(gpio->base + GPIO_DIR);
	iowrite32(reg & ~BIT(offset), gpio->base + GPIO_DIR);

	spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int __init aspeed_gpio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct aspeed_gpio *gpio;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	gpio->base = devm_ioremap_nocache(&pdev->dev, res->start,
					  resource_size(res));
	if (!gpio->base)
		return -ENOMEM;

	spin_lock_init(&gpio->lock);

	gpio->chip.ngpio = 32;

	gpio->chip.dev = &pdev->dev;
	gpio->chip.direction_input = aspeed_gpio_dir_in;
	gpio->chip.direction_output = aspeed_gpio_dir_out;
	gpio->chip.get = aspeed_gpio_get;
	gpio->chip.set = aspeed_gpio_set;
	gpio->chip.label = dev_name(&pdev->dev);
	gpio->chip.base = -1;

	platform_set_drvdata(pdev, gpio);

	return gpiochip_add(&gpio->chip);
}

static int aspeed_gpio_remove(struct platform_device *pdev)
{
	struct aspeed_gpio *gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&gpio->chip);
	return 0;
}

static const struct of_device_id aspeed_gpio_of_table[] = {
	{ .compatible = "aspeed,ast2400-gpio" },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_gpio_of_table);

static struct platform_driver aspeed_gpio_driver = {
	.remove = aspeed_gpio_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = aspeed_gpio_of_table,
	},
};

module_platform_driver_probe(aspeed_gpio_driver, aspeed_gpio_probe);

MODULE_DESCRIPTION("Aspeed AST2400 GPIO Driver");
