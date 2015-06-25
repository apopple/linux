/*
 * Copyright 2015 IBM Corporation
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>

#define DEFAULT_I2C_CLK_HZ	100000			/* max 400 Kbits/s */
#define ASPEED_I2C_TIMEOUT	msecs_to_jiffies(100)	/* transfer timeout */
#define AUTOSUSPEND_TIMEOUT	2000

struct ast_i2c {
	struct device *dev;
	struct i2c_adapter adapter;
	void __iomem *base;
	struct clk* clk;
	int size;
	int irq;
};

static u32 ast_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL
		| I2C_FUNC_SMBUS_READ_BLOCK_DATA;
}


static int ast_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msg, int num)
{
	struct ast_i2c *dev = i2c_get_adapdata(adap);
	int ret;
//	struct i2c_msg *m_start = msg;

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0)
		goto out;

out:
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return ret;
}

const static struct i2c_algorithm ast_i2c_algorithm = {
	.master_xfer	= ast_i2c_xfer,
	.functionality	= ast_i2c_func,
};

#define I2C_ISR_MASK	0x00
#define I2C_ISR_TGT	0x08

#define I2C_DEV_CR	0x00
#define I2C_DEV_TMR1	0x04
#define I2C_DEV_TMR2	0x08
#define I2C_DEV_INTCR	0x0c
#define I2C_DEV_ISR	0x10
#define I2C_DEV_STATUS	0x14
#define I2C_DEV_ADDR	0x18
#define I2C_DEV_BUFCR	0x1c
#define I2C_DEV_TXRX	0x20

#define SLAVE_MODE	0x00000001

static unsigned ast_i2c_read(struct ast_i2c *i2c, unsigned reg)
{
	unsigned ret = readl(i2c->base + reg);
	dev_dbg(i2c->dev, "read reg %p val %08x\n", i2c->base + reg, ret);
	return ret;
}

static void ast_i2c_write(struct ast_i2c *i2c, unsigned reg, unsigned val)
{
	writel(val, i2c->base + reg);
	dev_dbg(i2c->dev, "write reg %p val %08x\n", i2c->base + reg, val);
}

static void ast_disable_i2c_interrupts(struct ast_i2c *dev)
{
}

static void ast_init_i2c_bus(struct ast_i2c *i2c)
{
#if 0
	ast_disable_i2c_interrupts(i2c);

	ast_i2c_write(i2c, 0x00000001, I2C_DEV_CR);
	/* Hard code 100kHz assuming PCLK of 50MHz */
	ast_i2c_write(i2c, 0x77777355, I2C_DEV_TMR1);
	ast_i2c_write(i2c, 0x00000000, I2C_DEV_TMR2);
	/* Clear interrupt status */
	ast_i2c_write(i2c, 0xffffffff, I2C_DEV_ISR);
	/* Enable interrupt */
	ast_i2c_write(i2c, 0x000000bf, I2C_DEV_INTCR);
#endif
}

/*
 * Calculate symmetric clock as stated in datasheet:
 * twi_clk = F_MAIN / (2 * (cdiv * (1 << ckdiv) + offset))
 */
static void ast_calc_i2c_clock(struct ast_i2c *i2c, int i2c_clk)
{
#if 0
	int ckdiv, cdiv, div;
	struct ast_i2c_pdata *pdata = dev->pdata;
	int offset = pdata->clk_offset;
	int max_ckdiv = pdata->clk_max_div;

	div = max(0, (int)DIV_ROUND_UP(clk_get_rate(dev->clk),
				       2 * i2c_clk) - offset);
	ckdiv = fls(div >> 8);
	cdiv = div >> ckdiv;

	if (ckdiv > max_ckdiv) {
		dev_warn(dev->dev, "%d exceeds ckdiv max value which is %d.\n",
			 ckdiv, max_ckdiv);
		ckdiv = max_ckdiv;
		cdiv = 255;
	}

	dev->i2c_cwgr_reg = (ckdiv << 16) | (cdiv << 8) | cdiv;
	dev_dbg(dev->dev, "cdiv %d ckdiv %d\n", cdiv, ckdiv);
#endif
}

static irqreturn_t ast_i2c_interrupt(int irq, void *dev_id)
{
	return IRQ_NONE;
}


static int ast_i2c_probe(struct platform_device *pdev)
{
	struct ast_i2c *i2c;
	struct resource *res;
	int rc;
	u32 bus_clk_rate;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;
	i2c->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	i2c->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	i2c->irq = platform_get_irq(pdev, 0);
	if (i2c->irq < 0)
		return i2c->irq;

	rc = devm_request_irq(&pdev->dev, i2c->irq, ast_i2c_interrupt, 0,
			      dev_name(i2c->dev), i2c);
	if (rc) {
		dev_err(i2c->dev, "cannot get irq %d: %d\n", i2c->irq, rc);
		return rc;
	}

	platform_set_drvdata(pdev, i2c);

	i2c->clk = devm_clk_get(i2c->dev, NULL);
	if (IS_ERR(i2c->clk)) {
		dev_err(i2c->dev, "no clock defined\n");
		return -ENODEV;
	}
	clk_prepare_enable(i2c->clk);

	rc = of_property_read_u32(i2c->dev->of_node, "clock-frequency",
			&bus_clk_rate);
	if (rc) {
		dev_warn(i2c->dev, "clock-frequency property not found, using default\n");
		bus_clk_rate = DEFAULT_I2C_CLK_HZ;
	}

	ast_calc_i2c_clock(i2c, bus_clk_rate);
	ast_init_i2c_bus(i2c);

	snprintf(i2c->adapter.name, sizeof(i2c->adapter.name), "AST2400");
	i2c_set_adapdata(&i2c->adapter, i2c);
	i2c->adapter.owner = THIS_MODULE;
	i2c->adapter.class = I2C_CLASS_DEPRECATED;
	i2c->adapter.algo = &ast_i2c_algorithm;
	i2c->adapter.dev.parent = i2c->dev;
	i2c->adapter.nr = pdev->id;
	i2c->adapter.timeout = ASPEED_I2C_TIMEOUT;
	i2c->adapter.dev.of_node = pdev->dev.of_node;

	pm_runtime_set_autosuspend_delay(i2c->dev, AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(i2c->dev);
	pm_runtime_set_active(i2c->dev);
	pm_runtime_enable(i2c->dev);

	rc = i2c_add_numbered_adapter(&i2c->adapter);
	if (rc) {
		dev_err(i2c->dev, "adapter %s registration failed\n",
			i2c->adapter.name);
		clk_disable_unprepare(i2c->clk);

		pm_runtime_disable(i2c->dev);
		pm_runtime_set_suspended(i2c->dev);

		return rc;
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ast_i2c_of_match_table[] = {
	{
		.compatible = "aspeed,ast2400-i2c",
	},
	{ }
};
#endif

static struct platform_driver ast_i2c_driver = {
	.driver		= {
		.name	= "ast_i2c",
		.of_match_table = of_match_ptr(ast_i2c_of_match_table),
	},
};

module_platform_driver_probe(ast_i2c_driver, ast_i2c_probe);

MODULE_DESCRIPTION("Aspeed AST24xx i2c driver");
MODULE_AUTHOR("Joel Stanley <joel@jms.id.au>");
MODULE_LICENSE("GPL");
