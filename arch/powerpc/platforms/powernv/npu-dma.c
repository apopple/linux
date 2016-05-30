/*
 * This file implements the DMA operations for NVLink devices. The NPU
 * devices all point to the same iommu table as the parent PCI device.
 *
 * Copyright Alistair Popple, IBM Corporation 2015.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/mmu_notifier.h>
#include <linux/mmu_context.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/list.h>

#include <asm/uaccess.h>
#include <asm/reg.h>
#include <asm/opal.h>
#include <asm/io.h>

#include <linux/export.h>
#include <linux/pci.h>
#include <linux/memblock.h>
#include <linux/iommu.h>

#include <asm/iommu.h>
#include <asm/pnv-pci.h>
#include <asm/msi_bitmap.h>
#include <asm/opal.h>

#include "powernv.h"
#include "pci.h"

/*
 * Other types of TCE cache invalidation are not functional in the
 * hardware.
 */
static struct pci_dev *get_pci_dev(struct device_node *dn)
{
	return PCI_DN(dn)->pcidev;
}

/* Given a NPU device get the associated PCI device. */
struct pci_dev *pnv_pci_get_gpu_dev(struct pci_dev *npdev)
{
	struct device_node *dn;
	struct pci_dev *gpdev;

	/* Get assoicated PCI device */
	dn = of_parse_phandle(npdev->dev.of_node, "ibm,gpu", 0);
	if (!dn)
		return NULL;

	gpdev = get_pci_dev(dn);
	of_node_put(dn);

	return gpdev;
}
EXPORT_SYMBOL(pnv_pci_get_gpu_dev);

/* Given the real PCI device get a linked NPU device. */
struct pci_dev *pnv_pci_get_npu_dev(struct pci_dev *gpdev, int index)
{
	struct device_node *dn;
	struct pci_dev *npdev;

	if (WARN_ON(!gpdev))
		return NULL;

	if (WARN_ON(!gpdev->dev.of_node))
		return NULL;

	/* Get assoicated PCI device */
	dn = of_parse_phandle(gpdev->dev.of_node, "ibm,npu", index);
	if (!dn)
		return NULL;

	npdev = get_pci_dev(dn);
	of_node_put(dn);

	return npdev;
}
EXPORT_SYMBOL(pnv_pci_get_npu_dev);

#define NPU_DMA_OP_UNSUPPORTED()					\
	dev_err_once(dev, "%s operation unsupported for NVLink devices\n", \
		__func__)

static void *dma_npu_alloc(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flag,
			   unsigned long attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
	return NULL;
}

static void dma_npu_free(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle,
			 unsigned long attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
}

static dma_addr_t dma_npu_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction direction,
				   unsigned long attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
	return 0;
}

static int dma_npu_map_sg(struct device *dev, struct scatterlist *sglist,
			  int nelems, enum dma_data_direction direction,
			  unsigned long attrs)
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

static struct dma_map_ops dma_npu_ops = {
	.map_page		= dma_npu_map_page,
	.map_sg			= dma_npu_map_sg,
	.alloc			= dma_npu_alloc,
	.free			= dma_npu_free,
	.dma_supported		= dma_npu_dma_supported,
	.get_required_mask	= dma_npu_get_required_mask,
};

/*
 * Returns the PE assoicated with the PCI device of the given
 * NPU. Returns the linked pci device if pci_dev != NULL.
 */
static struct pnv_ioda_pe *get_gpu_pci_dev_and_pe(struct pnv_ioda_pe *npe,
						  struct pci_dev **gpdev)
{
	struct pnv_phb *phb;
	struct pci_controller *hose;
	struct pci_dev *pdev;
	struct pnv_ioda_pe *pe;
	struct pci_dn *pdn;

	pdev = pnv_pci_get_gpu_dev(npe->pdev);
	if (!pdev)
		return NULL;

	pdn = pci_get_pdn(pdev);
	if (WARN_ON(!pdn || pdn->pe_number == IODA_INVALID_PE))
		return NULL;

	hose = pci_bus_to_host(pdev->bus);
	phb = hose->private_data;
	pe = &phb->ioda.pe_array[pdn->pe_number];

	if (gpdev)
		*gpdev = pdev;

	return pe;
}

long pnv_npu_set_window(struct pnv_ioda_pe *npe, int num,
		struct iommu_table *tbl)
{
	struct pnv_phb *phb = npe->phb;
	int64_t rc;
	const unsigned long size = tbl->it_indirect_levels ?
		tbl->it_level_size : tbl->it_size;
	const __u64 start_addr = tbl->it_offset << tbl->it_page_shift;
	const __u64 win_size = tbl->it_size << tbl->it_page_shift;

	pe_info(npe, "Setting up window %llx..%llx pg=%lx\n",
			start_addr, start_addr + win_size - 1,
			IOMMU_PAGE_SIZE(tbl));

	rc = opal_pci_map_pe_dma_window(phb->opal_id,
			npe->pe_number,
			npe->pe_number,
			tbl->it_indirect_levels + 1,
			__pa(tbl->it_base),
			size << 3,
			IOMMU_PAGE_SIZE(tbl));
	if (rc) {
		pe_err(npe, "Failed to configure TCE table, err %lld\n", rc);
		return rc;
	}
	pnv_pci_phb3_tce_invalidate_entire(phb, false);

	/* Add the table to the list so its TCE cache will get invalidated */
	pnv_pci_link_table_and_group(phb->hose->node, num,
			tbl, &npe->table_group);

	return 0;
}

long pnv_npu_unset_window(struct pnv_ioda_pe *npe, int num)
{
	struct pnv_phb *phb = npe->phb;
	int64_t rc;

	pe_info(npe, "Removing DMA window\n");

	rc = opal_pci_map_pe_dma_window(phb->opal_id, npe->pe_number,
			npe->pe_number,
			0/* levels */, 0/* table address */,
			0/* table size */, 0/* page size */);
	if (rc) {
		pe_err(npe, "Unmapping failed, ret = %lld\n", rc);
		return rc;
	}
	pnv_pci_phb3_tce_invalidate_entire(phb, false);

	pnv_pci_unlink_table_and_group(npe->table_group.tables[num],
			&npe->table_group);

	return 0;
}

/*
 * Enables 32 bit DMA on NPU.
 */
static void pnv_npu_dma_set_32(struct pnv_ioda_pe *npe)
{
	struct pci_dev *gpdev;
	struct pnv_ioda_pe *gpe;
	int64_t rc;

	/*
	 * Find the assoicated PCI devices and get the dma window
	 * information from there.
	 */
	if (!npe->pdev || !(npe->flags & PNV_IODA_PE_DEV))
		return;

	gpe = get_gpu_pci_dev_and_pe(npe, &gpdev);
	if (!gpe)
		return;

	rc = pnv_npu_set_window(npe, 0, gpe->table_group.tables[0]);

	/*
	 * We don't initialise npu_pe->tce32_table as we always use
	 * dma_npu_ops which are nops.
	 */
	set_dma_ops(&npe->pdev->dev, &dma_npu_ops);
}

/*
 * Enables bypass mode on the NPU. The NPU only supports one
 * window per link, so bypass needs to be explicitly enabled or
 * disabled. Unlike for a PHB3 bypass and non-bypass modes can't be
 * active at the same time.
 */
static int pnv_npu_dma_set_bypass(struct pnv_ioda_pe *npe)
{
	struct pnv_phb *phb = npe->phb;
	int64_t rc = 0;
	phys_addr_t top = memblock_end_of_DRAM();

	if (phb->type != PNV_PHB_NPU || !npe->pdev)
		return -EINVAL;

	rc = pnv_npu_unset_window(npe, 0);
	if (rc != OPAL_SUCCESS)
		return rc;

	/* Enable the bypass window */

	top = roundup_pow_of_two(top);
	dev_info(&npe->pdev->dev, "Enabling bypass for PE %d\n",
			npe->pe_number);
	rc = opal_pci_map_pe_dma_window_real(phb->opal_id,
			npe->pe_number, npe->pe_number,
			0 /* bypass base */, top);

	if (rc == OPAL_SUCCESS)
		pnv_pci_phb3_tce_invalidate_entire(phb, false);

	return rc;
}

void pnv_npu_try_dma_set_bypass(struct pci_dev *gpdev, bool bypass)
{
	int i;
	struct pnv_phb *phb;
	struct pci_dn *pdn;
	struct pnv_ioda_pe *npe;
	struct pci_dev *npdev;

	for (i = 0; ; ++i) {
		npdev = pnv_pci_get_npu_dev(gpdev, i);

		if (!npdev)
			break;

		pdn = pci_get_pdn(npdev);
		if (WARN_ON(!pdn || pdn->pe_number == IODA_INVALID_PE))
			return;

		phb = pci_bus_to_host(npdev->bus)->private_data;

		/* We only do bypass if it's enabled on the linked device */
		npe = &phb->ioda.pe_array[pdn->pe_number];

		if (bypass) {
			dev_info(&npdev->dev,
					"Using 64-bit DMA iommu bypass\n");
			pnv_npu_dma_set_bypass(npe);
		} else {
			dev_info(&npdev->dev, "Using 32-bit DMA via iommu\n");
			pnv_npu_dma_set_32(npe);
		}
	}
}

/* Switch ownership from platform code to external user (e.g. VFIO) */
void pnv_npu_take_ownership(struct pnv_ioda_pe *npe)
{
	struct pnv_phb *phb = npe->phb;
	int64_t rc;

	/*
	 * Note: NPU has just a single TVE in the hardware which means that
	 * while used by the kernel, it can have either 32bit window or
	 * DMA bypass but never both. So we deconfigure 32bit window only
	 * if it was enabled at the moment of ownership change.
	 */
	if (npe->table_group.tables[0]) {
		pnv_npu_unset_window(npe, 0);
		return;
	}

	/* Disable bypass */
	rc = opal_pci_map_pe_dma_window_real(phb->opal_id,
			npe->pe_number, npe->pe_number,
			0 /* bypass base */, 0);
	if (rc) {
		pe_err(npe, "Failed to disable bypass, err %lld\n", rc);
		return;
	}
	pnv_pci_phb3_tce_invalidate_entire(npe->phb, false);
}

struct pnv_ioda_pe *pnv_pci_npu_setup_iommu(struct pnv_ioda_pe *npe)
{
	struct pnv_phb *phb = npe->phb;
	struct pci_bus *pbus = phb->hose->bus;
	struct pci_dev *npdev, *gpdev = NULL, *gptmp;
	struct pnv_ioda_pe *gpe = get_gpu_pci_dev_and_pe(npe, &gpdev);

	if (!gpe || !gpdev)
		return NULL;

	list_for_each_entry(npdev, &pbus->devices, bus_list) {
		gptmp = pnv_pci_get_gpu_dev(npdev);

		if (gptmp != gpdev)
			continue;

		pe_info(gpe, "Attached NPU %s\n", dev_name(&npdev->dev));
		iommu_group_add_device(gpe->table_group.group, &npdev->dev);
	}

	return gpe;
}

struct npu_context {
	spinlock_t lock;
	struct kref refcount;
	struct mm_struct *mm;
	struct mmu_notifier mn;
	struct npu *npu;
	int id;
};

#define npu_to_phb(x) container_of(x, struct pnv_phb, npu)

/* Get an mm_struct from an npu_context making sure it is still active
 * in the GPU and increment mm_count. May return NULL if the mm_struct
 * no longer exists or is in the process of being destroyed. */
static struct mm_struct *mm_from_npu_context(npu_context context)
{
	struct mm_struct *mm;

	/* TODO: We probably should move to at least an rwlock here or
	 * even rcu depending how much contention this lock actually
	 * has with the mmu notifiers and task registration. */
	spin_lock(&context->lock);

	mm = context->mm;
	if (!mm)
		/* mm no longer exists */
		goto out;

	/* We need to call use_mm() on the mm but first we need to
	 * make sure the mm isn't already in the process of being
	 * destroyed. If it is about to be destroyed (ie. mm_count ==
	 * 0) then we should bail. If it isn't we need to make sure it
	 * doesn't get destroyed before the call to use_mm(). */
	if (atomic_inc_return(&mm->mm_count) == 1) {
		atomic_dec(&mm->mm_count);
		mm = NULL;
	}

out:
	spin_unlock(&context->lock);

	return mm;
}

static void destroy_npu_context(struct kref *refcount)
{
	struct npu_context *context = container_of(refcount, struct npu_context, refcount);

	kfree(context);
}

/*
 * Find a free MMIO ATSD register and mark it in use. Return -ENOSPC
 * if none are available.
 */
static int get_mmio_atsd_reg(struct npu *npu)
{
	int i;

	for (i = 0; i < npu->mmio_atsd_count; i++) {
		if (test_and_set_bit(i, &npu->mmio_atsd_usage))
			return i;
	}

	return -ENOSPC;
}

static void put_mmio_atsd_reg(struct npu *npu, int reg)
{
	clear_bit(reg, &npu->mmio_atsd_usage);
}

#define iowrite64be writeq_be
#define ioread64be readq_be

static void mmio_invalidate_pid(struct npu *npu, unsigned long pid)
{
	int mmio_atsd_reg;
	unsigned long launch;

	do {
		mmio_atsd_reg = get_mmio_atsd_reg(npu);
	} while (mmio_atsd_reg < 0);

	/* Radix mode */
	launch = PPC_BIT(0);

	/* RIC */
	launch |= 2UL << PPC_BITLSHIFT(2);

	/* IS */
	launch |= PPC_BIT(12);

	/* PRS */
	launch |= PPC_BIT(13);

	/* AP */
	launch |= (u64) mmu_get_ap(mmu_virtual_psize) << PPC_BITLSHIFT(17);

	/* L */
	launch |= PPC_BIT(18);

	/* PID */
	launch |= pid << PPC_BITLSHIFT(38);

	/* Invalidating the entire process shouldn't need a va */
	iowrite64be(0, npu->mmio_atsd_regs[mmio_atsd_reg] + 1);
	iowrite64be(launch, npu->mmio_atsd_regs[mmio_atsd_reg]);

	/* Wait for completion */
	while (ioread64be(npu->mmio_atsd_regs[mmio_atsd_reg] + 2));
	put_mmio_atsd_reg(npu, mmio_atsd_reg);
}

static void mmio_invalidate_va(struct npu *npu, unsigned long va,
			unsigned long pid)
{
	int mmio_atsd_reg;
	unsigned long launch;

	do {
		mmio_atsd_reg = get_mmio_atsd_reg(npu);
	} while (mmio_atsd_reg < 0);

	/* Radix mode */
	launch = PPC_BIT(0);

	/* PRS */
	launch |= PPC_BIT(13);

	/* AP */
	launch |= (u64) mmu_get_ap(mmu_virtual_psize) << PPC_BITLSHIFT(17);

	/* L */
	launch |= PPC_BIT(18);

	/* PID */
	launch |= pid << PPC_BITLSHIFT(38);

	iowrite64be(va, npu->mmio_atsd_regs[mmio_atsd_reg] + 1);
	iowrite64be(launch, npu->mmio_atsd_regs[mmio_atsd_reg]);

	/* Wait for completion */
	while (ioread64be(npu->mmio_atsd_regs[mmio_atsd_reg] + 2));
	put_mmio_atsd_reg(npu, mmio_atsd_reg);
}

#define mn_to_npu_context(x) container_of(x, struct npu_context, mn)

static void pnv_npu2_mn_release(struct mmu_notifier *mn,
				struct mm_struct *mm)
{
	struct npu_context *context = mn_to_npu_context(mn);
	struct npu *npu = context->npu;
	struct pnv_phb *phb;

	phb = npu_to_phb(npu);

	spin_lock(&context->lock);
	BUG_ON(context->id == NV_NMMU_CONTEXT_INVALID);

	/* We need to remove the context from the tables */
	pr_info("NMMU Context %d removed\n", context->id);
	opal_npu_destroy_context(phb->opal_id, context->id);
	context->mm = NULL;
	context->id = NV_NMMU_CONTEXT_INVALID;
	mmio_invalidate_pid(npu, mm->context.id);
	spin_unlock(&context->lock);
	kref_put(&context->refcount, destroy_npu_context);

	return;
}

static void pnv_npu2_mn_change_pte(struct mmu_notifier *mn,
				struct mm_struct *mm,
				unsigned long address,
				pte_t pte)
{
	struct npu *npu = mn_to_npu_context(mn)->npu;

	mmio_invalidate_va(npu, address, mm->context.id);
}

static void pnv_npu2_mn_invalidate_page(struct mmu_notifier *mn,
					struct mm_struct *mm,
					unsigned long address)
{
	struct npu *npu = mn_to_npu_context(mn)->npu;

	mmio_invalidate_va(npu, address, mm->context.id);
}

static void pnv_npu2_mn_invalidate_range(struct mmu_notifier *mn,
					struct mm_struct *mm,
					unsigned long start, unsigned long end)
{
	struct npu *npu = mn_to_npu_context(mn)->npu;
	unsigned long address;

	for (address = start; address <= end; address += PAGE_SIZE)
		mmio_invalidate_va(npu, address, mm->context.id);
}

static const struct mmu_notifier_ops nv_nmmu_notifier_ops = {
	.release = pnv_npu2_mn_release,
	.change_pte = pnv_npu2_mn_change_pte,
	.invalidate_page = pnv_npu2_mn_invalidate_page,
	.invalidate_range = pnv_npu2_mn_invalidate_range,
};

/*
 * Call into OPAL to setup the nmmu context for the current task in
 * the NPU. This must be called to setup the context tables before the
 * GPU issues ATRs. pdev should be a pointed to PCIe GPU device.
 *
 * Returns an error if there are no contexts currently available
 * (should only happen on DD1) or a context_id which should be passed
 * to pnv_npu2_handle_fault().
 */
npu_context pnv_npu2_init_context(struct pci_dev *gpdev, unsigned long flags)
{
	int id;
	int lpid = 0;
	struct mm_struct *mm = current->mm;
	struct npu_context *npu_context;
	struct pnv_phb *nphb;
	struct npu *npu;

	/* The gpdev should have at least one nvlink (index 0)
	 * associated with it. It's possible we could have multiple
	 * links going to the same GPU but different NPUs
	 * (chips). However this seems unlikely so we assume this
	 * isn't the case, otherwise we would need to search all
	 * possible indicies. */
	struct pci_dev *npdev = pnv_pci_get_npu_dev(gpdev, 0);

	if (!npdev)
		/* No nvlink associated with this GPU device */
		return ERR_PTR(-ENODEV);

	if (!mm) {
		printk(KERN_ALERT "Init context should not be called for a kernel thread\n");
		return ERR_PTR(-EINVAL);
	}

	nphb = pci_bus_to_host(npdev->bus)->private_data;
	npu = &nphb->npu;

	/* Return an error if the context is already setup */
	if (mm->context.npu[npu->index])
		return ERR_PTR(-EEXIST);

	/* Setup the NPU context tables */
	id = opal_npu_init_context(nphb->opal_id, mm->context.id, flags, lpid);
	if (id < 0)
		return ERR_PTR(-ENOSPC);

	npu_context = kzalloc(sizeof(struct npu_context), GFP_KERNEL);
	if (!npu_context)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&npu_context->lock);
	kref_init(&npu_context->refcount);
	npu_context->id = id;
	npu_context->mn.ops = &nv_nmmu_notifier_ops;
	npu_context->npu = npu;
	npu_context->mm = mm;
	mmu_notifier_register(&npu_context->mn, mm);

	/* We pass a copy of the pointer out (as a npu_context) and
	 * keep one copy for use by the kernel so we need to increment
	 * the refcount. */
	kref_get(&npu_context->refcount);
	mm->context.npu[npu->index] = npu_context;

	return npu_context;
}
EXPORT_SYMBOL(pnv_npu2_init_context);

int pnv_npu2_destroy_context(npu_context context)
{
	kref_put(&context->refcount, destroy_npu_context);
       	return 0;
}
EXPORT_SYMBOL(pnv_npu2_destroy_context);

/*
 * Must be called from a kernel thread.
 */
int pnv_npu2_handle_fault(npu_context context, uintptr_t *ea,
			unsigned long *flags, unsigned long *status, int count)
{
	u64 tmp, rc = 0;
	int i;
	struct mm_struct *mm;
	unsigned long is_write;

	mm = mm_from_npu_context(context);
	if (!mm)
		return -ENOENT;

	use_mm(mm);

	/* mm_from_npu_context() above increments mm_count as does use_mm()
	 * so drop the count. */
	atomic_dec(&mm->mm_count);

	might_fault();

	for (i = 0; i < count; i++) {
		if (WARN_ON(ea[i] == CONFIG_KERNEL_START)) {
			status[i] = -EINVAL;
			continue;
		}

		is_write = flags[i] & NPU2_WRITE;
		if (is_write)
			/* To fault a writable page in we need to do a nop
			 * write. We could just do a lwarx/stwcx however this
			 * could result in two faults (one for the read and
			 * another for the write).
			 *
			 * Instead do a lwarx from a location that shouldn't
			 * fault (KERNEL_BASE) to clear any potential dangling
			 * reservations for ea and then do a stwcx which will
			 * cause a write fault but won't actually write any
			 * data because the reservation won't match. */
			__asm__ __volatile__(
				"	lwarx	%0,0,%2\n"
				"1:	stwcx.	%0,0,%3\n"
				"	li	%1, 0\n"
				"	b	+8\n"
				"2:	li	%1,-14\n"
				".section __ex_table,\"a\"\n"
				".llong 1b,2b\n"
				".previous\n"
				: "=&r" (tmp), "=r" (rc)
				: "r" (CONFIG_KERNEL_START), "r" (ea[i])
				: "cc");
		else
			rc = get_user(tmp, (u64 *) ea[i]);

		/* Some faults may only be prefetch faults so record
		 * the status and continue processing remaining
		 * faults. */
		status[i] = rc;
	}

	unuse_mm(mm);

	return 0;
}
EXPORT_SYMBOL(pnv_npu2_handle_fault);

int pnv_npu2_init(struct pnv_phb *phb)
{
	unsigned int i;
	u64 mmio_atsd;
	struct device_node *dn;
	struct pci_dev *gpdev;
	static int npu_index = 0;
	uint64_t rc = 0;

	for_each_child_of_node(phb->hose->dn, dn) {
		gpdev = pnv_pci_get_gpu_dev(get_pci_dev(dn));
		if (gpdev) {
			rc = opal_npu_map_lpar(phb->opal_id,
					PCI_DEVID(gpdev->bus->number, gpdev->devfn),
					0, 0);
			if (rc)
				dev_err(&gpdev->dev, "Error %lld mapping device to LPAR\n",
					rc);
		}
	}

	for (i = 0; !of_property_read_u64_index(phb->hose->dn, "ibm,mmio-atsd", i, &mmio_atsd); i++)
		phb->npu.mmio_atsd_regs[i] = ioremap(mmio_atsd, 32);

	pr_info("NPU%lld: Found %d MMIO ATSD registers", phb->opal_id, i);
	phb->npu.mmio_atsd_count = i;
	phb->npu.mmio_atsd_usage = 0;
	phb->npu.index = npu_index++;

	return 0;
}
