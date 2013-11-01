/* drivers/net/ethernet/ibm/emac/rgmii_wol.h
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

#ifndef __IBM_NEWEMAC_RGMII_WOL_H
#define __IBM_NEWEMAC_RGMII_WOL_H

/* RGMII device */
struct rgmii_wol_instance {
	u32 __iomem			*reg;

	/* RGMII bridge flags */
	int				flags;
#define EMAC_RGMII_FLAG_HAS_MDIO	0x00000001

	/* Only one EMAC whacks us at a time */
	struct mutex			lock;

	/* number of EMACs using this RGMII bridge */
	int				users;
};

#ifdef CONFIG_IBM_EMAC_RGMII_WOL

extern int rgmii_wol_init(void);
extern void rgmii_wol_exit(void);
extern int rgmii_wol_attach(struct platform_device *ofdev, int mode);
extern void rgmii_wol_detach(struct platform_device *ofdev);
extern void rgmii_wol_get_mdio(struct platform_device *ofdev);
extern void rgmii_wol_put_mdio(struct platform_device *ofdev);
extern void rgmii_wol_set_speed(struct platform_device *ofdev, int speed);
extern int rgmii_wol_get_regs_len(struct platform_device *ofdev);
extern void *rgmii_wol_dump_regs(struct platform_device *ofdev, void *buf);

#else

# define rgmii_wol_init()		0
# define rgmii_wol_exit()		do { } while (0)
# define rgmii_wol_attach(x, y)		(-ENXIO)
# define rgmii_wol_detach(x)		do { } while (0)
# define rgmii_wol_get_mdio(o)		do { } while (0)
# define rgmii_wol_put_mdio(o)		do { } while (0)
# define rgmii_wol_set_speed(x, y)	do { } while (0)
# define rgmii_wol_get_regs_len(x)	0
# define rgmii_wol_dump_regs(x, buf)	(buf)
#endif				/* !CONFIG_IBM_EMAC_RGMII_WOL */

#endif /* __IBM_NEWEMAC_RGMII_WOL_H */
