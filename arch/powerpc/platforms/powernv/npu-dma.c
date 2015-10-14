/*
 * This file implements the DMA operations for Nvlink devices. The NPU
 * devices all point to the same iommu table as the parent PCI device.
 *
 * Copyright Alistair Popple, IBM Corporation 2015.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/export.h>
#include <linux/pci.h>
#include <linux/memblock.h>

#include <asm/iommu.h>
#include <asm/pnv-pci.h>
#include <asm/msi_bitmap.h>
#include <asm/opal.h>

#include "powernv.h"
#include "pci.h"

static struct pci_dev *get_pci_dev(struct device_node *dn)
{
	return PCI_DN(dn)->pcidev;
}

/* Given a NPU device get the associated PCI device. */
struct pci_dev *pnv_get_nvl_pci_dev(struct pci_dev *nvl_dev)
{
	struct device_node *dn;
	struct pci_dev *pci_dev;

	/* Get assoicated PCI device */
	dn = of_parse_phandle(nvl_dev->dev.of_node, "ibm,gpu", 0);
	if (!dn)
		return NULL;

	pci_dev = get_pci_dev(dn);
	of_node_put(dn);

	return pci_dev;
}
EXPORT_SYMBOL(pnv_get_nvl_pci_dev);

/* Given the real PCI device get a linked NPU device. */
struct pci_dev *pnv_get_pci_nvl_dev(struct pci_dev *pci_dev, int index)
{
	struct device_node *dn;
	struct pci_dev *nvl_dev;

	/* Get assoicated PCI device */
	dn = of_parse_phandle(pci_dev->dev.of_node, "ibm,npu", index);
	if (!dn)
		return NULL;

	nvl_dev = get_pci_dev(dn);
	of_node_put(dn);

	return nvl_dev;
}
EXPORT_SYMBOL(pnv_get_pci_nvl_dev);

const struct dma_map_ops *get_linked_pci_dma_map_ops(struct device *dev,
					struct pci_dev **pci_dev)
{
	*pci_dev = pnv_get_nvl_pci_dev(to_pci_dev(dev));
	if (!*pci_dev)
		return NULL;

	return get_dma_ops(&(*pci_dev)->dev);
}

#define NPU_DMA_OP_UNSUPPORTED()					\
	dev_err_once(dev, "%s operation unsupported for Nvlink devices\n", \
		__func__)

static void *dma_npu_alloc(struct device *dev, size_t size,
				      dma_addr_t *dma_handle, gfp_t flag,
				      struct dma_attrs *attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
	return NULL;
}

static void dma_npu_free(struct device *dev, size_t size,
				    void *vaddr, dma_addr_t dma_handle,
				    struct dma_attrs *attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
}

static dma_addr_t dma_npu_map_page(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction direction,
				     struct dma_attrs *attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
	return 0;
}

static int dma_npu_map_sg(struct device *dev, struct scatterlist *sglist,
			    int nelems, enum dma_data_direction direction,
			    struct dma_attrs *attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
	return 0;
}

static int dma_npu_dma_supported(struct device *dev, u64 mask)
{
	NPU_DMA_OP_UNSUPPORTED();
	return 0;
}

static u64 dma_npu_get_required_mask(struct device *dev)
{
	NPU_DMA_OP_UNSUPPORTED();
	return 0;
}

struct dma_map_ops dma_npu_ops = {
	.map_page		= dma_npu_map_page,
	.map_sg			= dma_npu_map_sg,
	.alloc			= dma_npu_alloc,
	.free			= dma_npu_free,
	.dma_supported		= dma_npu_dma_supported,
	.get_required_mask	= dma_npu_get_required_mask,
};

/* Returns the PE assoicated with the PCI device of the given
 * NPU. Returns the linked pci device if pci_dev != NULL.
 */
static struct pnv_ioda_pe *get_linked_pci_pe(struct pci_dev *npu_dev,
					struct pci_dev **pci_dev)
{
	struct pci_dev *linked_pci_dev;
	struct pci_controller *pci_hose;
	struct pnv_phb *pci_phb;
	struct pnv_ioda_pe *linked_pe;
	unsigned long pe_num;

	linked_pci_dev = pnv_get_nvl_pci_dev(npu_dev);
	if (!linked_pci_dev)
		return NULL;

	pci_hose = pci_bus_to_host(linked_pci_dev->bus);
	pci_phb = pci_hose->private_data;
	pe_num = pci_get_pdn(linked_pci_dev)->pe_number;
	if (pe_num == IODA_INVALID_PE)
		return NULL;

	linked_pe = &pci_phb->ioda.pe_array[pe_num];
	if (pci_dev)
		*pci_dev = linked_pci_dev;

	return linked_pe;
}

/* For the NPU we want to point the TCE table at the same table as the
 * real PCI device.
 */
void pnv_pci_npu_setup_dma_pe(struct pnv_phb *npu,
			struct pnv_ioda_pe *npu_pe)
{
	void *addr;
	struct pci_dev *pci_dev;
	struct pnv_ioda_pe *pci_pe;
	unsigned int tce_table_size;
	int rc;

	/* Find the assoicated PCI devices and get the dma window
	 * information from there.
	 */
	if (!npu_pe->pdev || !(npu_pe->flags & PNV_IODA_PE_DEV))
		return;

	pci_pe = get_linked_pci_pe(npu_pe->pdev, &pci_dev);
	if (!pci_pe)
		return;

	addr = (void *) pci_pe->table_group.tables[0]->it_base;
	tce_table_size = pci_pe->table_group.tables[0]->it_size << 3;
	rc = opal_pci_map_pe_dma_window(npu->opal_id, npu_pe->pe_number,
					npu_pe->pe_number, 1, __pa(addr),
					tce_table_size, 0x1000);
	WARN_ON(rc != OPAL_SUCCESS);

	/* We don't initialise npu_pe->tce32_table as we always use
	 * dma_npu_ops which redirects to the actual pci device dma op
	 * functions.
	 */
	set_dma_ops(&npu_pe->pdev->dev, &dma_npu_ops);
}

/* Enable/disable bypass mode on the NPU. The NPU only supports one
 * window per brick, so bypass needs to be explicity enabled or
 * disabled. Unlike for a PHB3 bypass and non-bypass modes can't be
 * active at the same time.
 */
int pnv_pci_npu_dma_set_bypass(struct pnv_phb *npu,
			struct pnv_ioda_pe *npu_pe, bool enabled)
{
	int rc = 0;

	if (npu->type != PNV_PHB_NPU)
		return -EINVAL;

	if (enabled) {
		/* Enable the bypass window */
		phys_addr_t top = memblock_end_of_DRAM();

		npu_pe->tce_bypass_base = 0;
		top = roundup_pow_of_two(top);
		dev_info(&npu_pe->pdev->dev, "Enabling bypass for PE %d\n",
			npu_pe->pe_number);
		rc = opal_pci_map_pe_dma_window_real(npu->opal_id,
						     npu_pe->pe_number,
						     npu_pe->pe_number,
						     npu_pe->tce_bypass_base,
						     top);
	} else
		/* Disable the bypass window by replacing it with the
		 * TCE32 window.
		 */
		pnv_pci_npu_setup_dma_pe(npu, npu_pe);

	return rc;
}

int pnv_npu_dma_set_mask(struct pci_dev *pdev, u64 dma_mask)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct pci_dn *pdn = pci_get_pdn(pdev);
	struct pnv_ioda_pe *pe, *linked_pe;
	struct pci_dev *linked_pci_dev;
	uint64_t top;
	bool bypass = false;

	if (WARN_ON(!pdn || pdn->pe_number == IODA_INVALID_PE))
		return -ENODEV;


	/* We only do bypass if it's enabled on the linked device */
	linked_pe = get_linked_pci_pe(pdev, &linked_pci_dev);
	if (!linked_pe)
		return -ENODEV;

	if (linked_pe->tce_bypass_enabled) {
		top = linked_pe->tce_bypass_base + memblock_end_of_DRAM() - 1;
		bypass = (dma_mask >= top);
	}

	if (bypass)
		dev_info(&pdev->dev, "Using 64-bit DMA iommu bypass\n");
	else
		dev_info(&pdev->dev, "Using 32-bit DMA via iommu\n");

	pe = &phb->ioda.pe_array[pdn->pe_number];
	pnv_pci_npu_dma_set_bypass(phb, pe, bypass);
	*pdev->dev.dma_mask = dma_mask;

	return 0;
}
