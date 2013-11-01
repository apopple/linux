/* drivers/net/ethernet/ibm/emac/rgmii_wol.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, RGMII bridge with
 * wake on LAN support.
 *
 * Copyright 2013 Alistair Popple, IBM Corp.
 *                <alistair@popple.id.au>
 *
 * Based on rgmii.h:
 * Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *                <benh@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <linux/io.h>
#include <linux/of_address.h>

#include "emac.h"
#include "debug.h"

/* RGMII_WOL_REG */

#define WKUP_ETH_RGSPD      0xC0000000
#define WKUP_ETH_FCSEN      0x20000000
#define WKUP_ETH_CRSEN      0x02000000
#define WKUP_ETH_COLEN      0x01000000
#define WKUP_ETH_TX_OE      0x00040000
#define WKUP_ETH_RX_IE      0x00020000
#define WKUP_ETH_RGMIIEN    0x00010000

#define WKUP_ETH_RGSPD_10   0x00000000
#define WKUP_ETH_RGSPD_100  0x40000000
#define WKUP_ETH_RGSPD_1000 0x80000000

/* RGMII bridge supports only GMII/TBI and RGMII/RTBI PHYs */
static inline int rgmii_valid_mode(int phy_mode)
{
	return  phy_mode == PHY_MODE_GMII ||
		phy_mode == PHY_MODE_MII ||
		phy_mode == PHY_MODE_RGMII ||
		phy_mode == PHY_MODE_TBI ||
		phy_mode == PHY_MODE_RTBI;
}

int rgmii_wol_attach(struct platform_device *ofdev, int mode)
{
	struct rgmii_wol_instance *dev = platform_get_drvdata(ofdev);

	dev_dbg(&ofdev->dev, "attach\n");

	/* Check if we need to attach to a RGMII */
	if (!rgmii_valid_mode(mode)) {
		dev_err(&ofdev->dev, "unsupported settings !\n");
		return -ENODEV;
	}

	mutex_lock(&dev->lock);

	/* Enable this input */
	out_be32(dev->reg, (in_be32(dev->reg) | WKUP_ETH_RGMIIEN |
				    WKUP_ETH_TX_OE | WKUP_ETH_RX_IE));

	++dev->users;

	mutex_unlock(&dev->lock);

	return 0;
}

void rgmii_wol_set_speed(struct platform_device *ofdev, int speed)
{
	struct rgmii_wol_instance *dev = platform_get_drvdata(ofdev);
	u32 reg;

	mutex_lock(&dev->lock);

	reg = in_be32(dev->reg) & ~WKUP_ETH_RGSPD;

	dev_dbg(&ofdev->dev, "speed(%d)\n", speed);

	switch (speed) {
	case SPEED_1000:
		reg |= WKUP_ETH_RGSPD_1000;
		break;
	case SPEED_100:
		reg |= WKUP_ETH_RGSPD_100;
		break;
	case SPEED_10:
		reg |= WKUP_ETH_RGSPD_10;
		break;
	default:
		dev_err(&ofdev->dev, "invalid speed set!\n");
	}

	out_be32(dev->reg, reg);

	mutex_unlock(&dev->lock);
}

void rgmii_wol_get_mdio(struct platform_device *ofdev)
{
	/* MDIO is always enabled when RGMII_WOL is enabled, so we
	 * don't have to do anything here.
	 */
	dev_dbg(&ofdev->dev, "get_mdio\n");
}

void rgmii_wol_put_mdio(struct platform_device *ofdev)
{
	dev_dbg(&ofdev->dev, "put_mdio\n");
}

void rgmii_wol_detach(struct platform_device *ofdev)
{
	struct rgmii_wol_instance *dev = platform_get_drvdata(ofdev);

	BUG_ON(!dev || dev->users == 0);

	mutex_lock(&dev->lock);

	dev_dbg(&ofdev->dev, "detach\n");

	/* Disable this input */
	out_be32(dev->reg, 0);

	--dev->users;

	mutex_unlock(&dev->lock);
}

int rgmii_wol_get_regs_len(struct platform_device *ofdev)
{
	return sizeof(struct emac_ethtool_regs_subhdr) +
		sizeof(u32);
}

void *rgmii_wol_dump_regs(struct platform_device *ofdev, void *buf)
{
	struct rgmii_wol_instance *dev = platform_get_drvdata(ofdev);
	struct emac_ethtool_regs_subhdr *hdr = buf;
	u32 *regs = (u32 *)(hdr + 1);

	hdr->version = 0;
	hdr->index = 0; /* for now, are there chips with more than one
			 * rgmii ? if yes, then we'll add a cell_index
			 * like we do for emac
			 */
	memcpy_fromio(regs, dev->reg, sizeof(u32));
	return regs + 1;
}


static int rgmii_wol_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	struct rgmii_wol_instance *dev;
	int rc;

	rc = -ENOMEM;
	dev = kzalloc(sizeof(struct rgmii_wol_instance), GFP_KERNEL);
	if (dev == NULL)
		goto err_gone;

	mutex_init(&dev->lock);

	dev->reg = of_iomap(np, 0);
	if (!dev->reg) {
		dev_err(&ofdev->dev, "Can't map registers\n");
		rc = -ENXIO;
		goto err_free;
	}

	/* Check for RGMII flags */
	if (of_property_read_bool(np, "has-mdio"))
		dev->flags |= EMAC_RGMII_FLAG_HAS_MDIO;

	dev_dbg(&ofdev->dev, " Boot REG = 0x%08x\n", in_be32(dev->reg));

	/* Disable all inputs by default */
	out_be32(dev->reg, 0);

	dev_info(&ofdev->dev,
	       "RGMII %s initialized with%s MDIO support\n",
	       ofdev->dev.of_node->full_name,
	       (dev->flags & EMAC_RGMII_FLAG_HAS_MDIO) ? "" : "out");

	wmb();
	platform_set_drvdata(ofdev, dev);

	return 0;

 err_free:
	kfree(dev);
 err_gone:
	return rc;
}

static int rgmii_wol_remove(struct platform_device *ofdev)
{
	struct rgmii_wol_instance *dev = platform_get_drvdata(ofdev);

	WARN_ON(dev->users != 0);

	iounmap(dev->reg);
	kfree(dev);

	return 0;
}

static struct of_device_id rgmii_wol_match[] = {
	{
		.compatible	= "ibm,rgmii-wol",
	},
	{
		.type		= "emac-rgmii-wol",
	},
	{},
};

static struct platform_driver rgmii_wol_driver = {
	.driver = {
		.name = "emac-rgmii-wol",
		.owner = THIS_MODULE,
		.of_match_table = rgmii_wol_match,
	},
	.probe = rgmii_wol_probe,
	.remove = rgmii_wol_remove,
};

int __init rgmii_wol_init(void)
{
	return platform_driver_register(&rgmii_wol_driver);
}

void rgmii_wol_exit(void)
{
	platform_driver_unregister(&rgmii_wol_driver);
}
