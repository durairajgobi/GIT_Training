// SPDX-License-Identifier: GPL-2.0
/*
 * 077_dma_coherent.c - Coherent DMA buffer for a descriptor ring.
 *
 * Coherent memory is visible to CPU and device without explicit syncs;
 * ideal for small, long-lived structures like descriptor rings.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#define RING_ENTRIES 32

struct dma_desc {
	__le64 addr;
	__le32 len;
	__le32 flags;
};

struct dc_priv {
	struct dma_desc *ring;
	dma_addr_t ring_dma;
};

static struct platform_device *pdev_self;

static int dc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dc_priv *p;
	int ret;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(dev, ret, "no suitable DMA mask\n");

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->ring = dmam_alloc_coherent(dev, RING_ENTRIES * sizeof(*p->ring),
				      &p->ring_dma, GFP_KERNEL);
	if (!p->ring)
		return -ENOMEM;

	p->ring[0].len = cpu_to_le32(2048);
	p->ring[0].flags = cpu_to_le32(1);	/* OWN bit */
	/* a real driver would now write ring_dma to the device's base register */
	dev_info(dev, "ring cpu=%px dma=%pad\n", p->ring, &p->ring_dma);
	platform_set_drvdata(pdev, p);
	return 0;
}

static struct platform_driver dc_driver = {
	.probe = dc_probe,
	.driver = { .name = "training-dma-coherent" },
};

static int __init dc_init(void)
{
	int ret = platform_driver_register(&dc_driver);

	if (ret)
		return ret;
	pdev_self = platform_device_register_simple("training-dma-coherent", -1, NULL, 0);
	if (IS_ERR(pdev_self)) {
		platform_driver_unregister(&dc_driver);
		return PTR_ERR(pdev_self);
	}
	return 0;
}

static void __exit dc_exit(void)
{
	platform_device_unregister(pdev_self);
	platform_driver_unregister(&dc_driver);
}

module_init(dc_init);
module_exit(dc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Coherent DMA allocation example");
