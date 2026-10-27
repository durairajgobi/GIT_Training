// SPDX-License-Identifier: GPL-2.0
/*
 * 088_pci_skeleton.c - PCI driver: enable device, map BAR0, MSI interrupt.
 *
 * Matches the QEMU "edu" educational device (-device edu), handy for testing:
 *   qemu-system-x86_64 ... -device edu
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#define EDU_VENDOR 0x1234
#define EDU_DEVICE 0x11e8

#define EDU_REG_ID       0x00
#define EDU_REG_LIVENESS 0x04
#define EDU_REG_IRQ_STAT 0x24
#define EDU_REG_IRQ_RAISE 0x60
#define EDU_REG_IRQ_ACK  0x64

struct edu {
	void __iomem *bar0;
	int irq;
};

static irqreturn_t edu_irq(int irq, void *data)
{
	struct edu *e = data;
	u32 st = ioread32(e->bar0 + EDU_REG_IRQ_STAT);

	if (!st)
		return IRQ_NONE;
	iowrite32(st, e->bar0 + EDU_REG_IRQ_ACK);
	pr_info("edu: irq status 0x%x\n", st);
	return IRQ_HANDLED;
}

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct edu *e;
	int ret;

	e = devm_kzalloc(&pdev->dev, sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), "training_edu");
	if (ret)
		return ret;
	e->bar0 = pcim_iomap_table(pdev)[0];

	pci_set_master(pdev);
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
	if (ret)
		return ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_INTX);
	if (ret < 0)
		return ret;
	e->irq = pci_irq_vector(pdev, 0);
	ret = request_irq(e->irq, edu_irq, IRQF_SHARED, "training_edu", e);
	if (ret) {
		pci_free_irq_vectors(pdev);
		return ret;
	}

	pci_set_drvdata(pdev, e);
	dev_info(&pdev->dev, "id=0x%08x\n", ioread32(e->bar0 + EDU_REG_ID));

	iowrite32(0x12345678, e->bar0 + EDU_REG_LIVENESS);
	dev_info(&pdev->dev, "liveness check: 0x%08x (expect inverted)\n",
		 ioread32(e->bar0 + EDU_REG_LIVENESS));

	iowrite32(0x1, e->bar0 + EDU_REG_IRQ_RAISE);	/* trigger test interrupt */
	return 0;
}

static void edu_remove(struct pci_dev *pdev)
{
	struct edu *e = pci_get_drvdata(pdev);

	free_irq(e->irq, e);
	pci_free_irq_vectors(pdev);
	/* BAR mapping and enable are released by pcim_* automatically */
}

static const struct pci_device_id edu_ids[] = {
	{ PCI_DEVICE(EDU_VENDOR, EDU_DEVICE) },
	{ }
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
	.name     = "training_edu",
	.id_table = edu_ids,
	.probe    = edu_probe,
	.remove   = edu_remove,
};
module_pci_driver(edu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCI driver skeleton (QEMU edu device)");
