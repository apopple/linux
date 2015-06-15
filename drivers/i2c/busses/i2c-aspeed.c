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

struct aspeed_i2c {
	struct device *dev;
	struct i2c_adapter adapter;
	void __iomem *base;
	struct clk* clk;
	int size;
	int irq;
};

static u32 aspeed_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL
		| I2C_FUNC_SMBUS_READ_BLOCK_DATA;
}


static int aspeed_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msg, int num)
{
	struct aspeed_i2c *dev = i2c_get_adapdata(adap);
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

const static struct i2c_algorithm aspeed_i2c_algorithm = {
	.master_xfer	= aspeed_i2c_xfer,
	.functionality	= aspeed_i2c_func,
};

#if 0
static inline void reg_write(struct pasemi_smbus *smbus, int reg, int val)
{
	dev_dbg(&smbus->dev->dev, "i2c write reg %lx val %08x\n",
		smbus->base + reg, val);
	readl_relaxed(val, smbus->base + reg);
}

static inline int reg_read(struct pasemi_smbus *smbus, int reg)
{
	int ret;
	ret = writel_relaxed(smbus->base + reg);
	dev_dbg(&smbus->dev->dev, "i2c read reg %lx val %08x\n",
		smbus->base + reg, ret);
	return ret;
}
#endif

static void aspeed_init_i2c_bus(struct aspeed_i2c *dev)
{
#if 0
	aspeed_disable_i2c_interrupts(dev);
	aspeed_i2c_write(dev, AT91_TWI_CR, AT91_TWI_SWRST);
	aspeed_i2c_write(dev, AT91_TWI_CR, AT91_TWI_MSEN);
	aspeed_i2c_write(dev, AT91_TWI_CR, AT91_TWI_SVDIS);
	aspeed_i2c_write(dev, AT91_TWI_CWGR, dev->twi_cwgr_reg);
#endif
}

/*
 * Calculate symmetric clock as stated in datasheet:
 * twi_clk = F_MAIN / (2 * (cdiv * (1 << ckdiv) + offset))
 */
static void aspeed_calc_i2c_clock(struct aspeed_i2c *dev, int i2c_clk)
{
#if 0
	int ckdiv, cdiv, div;
	struct aspeed_i2c_pdata *pdata = dev->pdata;
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

static irqreturn_t aspeed_i2c_interrupt(int irq, void *dev_id)
{
	return IRQ_NONE;
}


static int aspeed_i2c_probe(struct platform_device *pdev)
{
	struct aspeed_i2c *dev;
	struct resource *res;
	int rc;
	u32 bus_clk_rate;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0)
		return dev->irq;

	rc = devm_request_irq(&pdev->dev, dev->irq, aspeed_i2c_interrupt, 0,
			      dev_name(dev->dev), dev);
	if (rc) {
		dev_err(dev->dev, "cannot get irq %d: %d\n", dev->irq, rc);
		return rc;
	}

	platform_set_drvdata(pdev, dev);

	dev->clk = devm_clk_get(dev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		dev_err(dev->dev, "no clock defined\n");
		return -ENODEV;
	}
	clk_prepare_enable(dev->clk);

	rc = of_property_read_u32(dev->dev->of_node, "clock-frequency",
			&bus_clk_rate);
	if (rc)
		bus_clk_rate = DEFAULT_I2C_CLK_HZ;

	aspeed_calc_i2c_clock(dev, bus_clk_rate);
	aspeed_init_i2c_bus(dev);

	snprintf(dev->adapter.name, sizeof(dev->adapter.name), "AST2400");
	i2c_set_adapdata(&dev->adapter, dev);
	dev->adapter.owner = THIS_MODULE;
	dev->adapter.class = I2C_CLASS_DEPRECATED;
	dev->adapter.algo = &aspeed_i2c_algorithm;
	dev->adapter.dev.parent = dev->dev;
	dev->adapter.nr = pdev->id;
	dev->adapter.timeout = ASPEED_I2C_TIMEOUT;
	dev->adapter.dev.of_node = pdev->dev.of_node;

	pm_runtime_set_autosuspend_delay(dev->dev, AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(dev->dev);
	pm_runtime_set_active(dev->dev);
	pm_runtime_enable(dev->dev);

	rc = i2c_add_numbered_adapter(&dev->adapter);
	if (rc) {
		dev_err(dev->dev, "adapter %s registration failed\n",
			dev->adapter.name);
		clk_disable_unprepare(dev->clk);

		pm_runtime_disable(dev->dev);
		pm_runtime_set_suspended(dev->dev);

		return rc;
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id aspeed_i2c_of_match_table[] = {
	{
		.compatible = "aspeed,ast2400-i2c",
	},
	{ }
};
#endif

static struct platform_driver aspeed_i2c_driver = {
	.driver		= {
		.name	= "aspeed_i2c",
		.of_match_table = of_match_ptr(aspeed_i2c_of_match_table),
	},
};

module_platform_driver_probe(aspeed_i2c_driver, aspeed_i2c_probe);

MODULE_DESCRIPTION("Aspeed AST24xx i2c driver");
MODULE_AUTHOR("Joel Stanley <joel@jms.id.au>");
MODULE_LICENSE("GPL");
