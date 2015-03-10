#include <asm/io.h>
#include <asm/pci.h>
#include <linux/pci.h>
#include <linux/module.h>

/* Just some data to test DMA operations with */
#define MAGIC_DATA 0x12345678

MODULE_AUTHOR("Alistair Popple <alistair.popple@au1.ibm.com>");

/* Breaks compiling as a module if any GPL symbols are used */
MODULE_LICENSE("Proprietory");

static const struct pci_device_id nvl_tbl[] = {
	{ 0x10de, 0x1234, PCI_ANY_ID, PCI_ANY_ID },
	{ 0x1014, 0x04ea, PCI_ANY_ID, PCI_ANY_ID },
	{ 0x1014, 0x04ea, PCI_ANY_ID, PCI_ANY_ID },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, nvl_tbl);

/* The next two functions will probably be incorporated into kernel platform code */
static struct pci_dev *pnv_get_nvl_pci_dev(struct pci_dev *nvl_dev)
{
	struct device_node *pci_dn;
	struct pci_dev *pci_dev;

	/* Get assoicated PCI device */
	pci_dn = of_parse_phandle(nvl_dev->dev.of_node, "ibm,gpu", 0);
	if (!pci_dn) {
		pr_alert("Unable to find real NVLink PCI device\n");
		return NULL;
	}

	pci_dev = PCI_DN(pci_dn)->pcidev;
	of_node_put(pci_dn);

	return pci_dev;
}

static struct pci_dev *pnv_get_pci_nvl_dev(struct pci_dev *pci_dev)
{
	struct device_node *nvl_dn;
	struct pci_dev *nvl_dev;

	/* Get assoicated PCI device */
	nvl_dn = of_parse_phandle(pci_dev->dev.of_node, "ibm,npu", 0);
	if (!nvl_dn) {
		pr_alert("Unable to find emulated NVLink PCI device\n");
		return NULL;
	}

	nvl_dev = PCI_DN(nvl_dn)->pcidev;
	of_node_put(nvl_dn);

	return nvl_dev;
}

static int nvl_probe_real_dev(struct pci_dev *pdev)
{
	/* Not implemented */
	return 0;
}

static int nvl_probe_fake_dev(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int i, pci_cap_vndr, proc_status = 0;
	void __iomem *dl_tl_regs = 0;
	void __iomem *pl_regs = 0;
	dma_addr_t dma_from_dev;
	u32 *data;
	struct pci_dev *real_pci_dev;

	/* Get the struct pci_dev for the real pci device */
	real_pci_dev = pnv_get_nvl_pci_dev(pdev);

	/* Map 64-bit BAR0/1 (TL/DL registers) */
	dl_tl_regs = pci_iomap(pdev, 0, 0);
	if (!dl_tl_regs) {
		pr_alert("Unable to map DL/TL registers\n");
		return -ENOMEM;
	}

	/* Map 64-bit BAR2/3 PL registers */
	if (ent->device == 0xffee) {
		pl_regs = pci_iomap(pdev, 2, 0);
		if (!pl_regs) {
			pr_alert("Unable to map PL registers\n");
			return -ENOMEM;
		}
	}

	data = kzalloc(4096, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Should enable TCE bypass mode (not implemented yet) */
	dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));

	/* Put some data in the buffer to transform */
	*data = MAGIC_DATA;

	/* Map a region to allow DMA writes to the data array */
	dma_from_dev = dma_map_single(&pdev->dev, data, 4096, DMA_BIDIRECTIONAL);
	if (dma_from_dev == DMA_ERROR_CODE) {
		pr_alert("Unable to map dma region\n");
	}

	/* Write the real address of the buffer to the device
	 * (triggers the data transformation) */
	iowrite32be(dma_from_dev, dl_tl_regs);

	pr_alert("read 0x%08x\n", ioread32be(dl_tl_regs));
	pr_alert("data at RA 0x%016lx mapped to PCI RA 0x%016llx\n", __pa(data), dma_from_dev);

	if (*data == ~MAGIC_DATA)
		pr_alert("data(= 0x%08x) correctly transformed\n", *data);
	else
		pr_alert("data(= 0x%08x) incorrectly transformed\n", *data);

	/* Test NPU procedures */
	pci_cap_vndr = pci_find_capability(pdev, PCI_CAP_ID_VNDR);
	if (!pci_cap_vndr)
		pr_alert("Unable to find vendor specific capability\n");
	else {
		pci_write_config_dword(pdev, pci_cap_vndr + 8, 0x1);
		for (i = 0; i < 7 && proc_status == 0; i++)
			pci_read_config_dword(pdev, pci_cap_vndr + 4, &proc_status);

		if (proc_status != 0x1)
			pr_alert("Timed out waiting for procedure to complete\n");
	}

	return 0;
}

static int nvl_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	if (ent->vendor == 0x10de) {
		pr_alert("Found real NPU PCI device\n");
		return nvl_probe_real_dev(pdev);
	} else {
		pr_alert("Found emulated/linked NPU PCI device\n");
		return nvl_probe_fake_dev(pdev, ent);
	}
}

static void nvl_remove(struct pci_dev *pdev)
{
	return;
}

static struct pci_driver nvlink_driver = {
	.name =		"NV-Link Test driver",
	.id_table =	nvl_tbl,
	.probe =	nvl_probe,
	.remove =	nvl_remove,
};

module_pci_driver(nvlink_driver);
