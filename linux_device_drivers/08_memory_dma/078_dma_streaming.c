// SPDX-License-Identifier: GPL-2.0
/*
 * 078_dma_streaming.c - Streaming DMA mappings: single buffer and scatterlist.
 *
 * Streaming mappings are used for per-transfer data buffers. Ownership
 * moves between CPU and device with dma_sync_* calls.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#define BUF_LEN 4096
#define NSG 4

static struct platform_device *pdev_self;

static int ds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct scatterlist sg[NSG];
	void *bufs[NSG] = { };
	dma_addr_t handle;
	void *buf;
	int i, nents, ret = 0;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	/* --- single buffer, CPU -> device --- */
	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	memset(buf, 0x5A, BUF_LEN);

	handle = dma_map_single(dev, buf, BUF_LEN, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, handle)) {
		kfree(buf);
		return -EIO;
	}
	dev_info(dev, "single mapping at %pad\n", &handle);
	/* ... device would read the buffer here ... */
	dma_unmap_single(dev, handle, BUF_LEN, DMA_TO_DEVICE);
	kfree(buf);

	/* --- scatter/gather, device -> CPU --- */
	sg_init_table(sg, NSG);
	for (i = 0; i < NSG; i++) {
		bufs[i] = kmalloc(BUF_LEN, GFP_KERNEL);
		if (!bufs[i]) {
			ret = -ENOMEM;
			goto free;
		}
		sg_set_buf(&sg[i], bufs[i], BUF_LEN);
	}

	nents = dma_map_sg(dev, sg, NSG, DMA_FROM_DEVICE);
	if (!nents) {
		ret = -EIO;
		goto free;
	}
	dev_info(dev, "sg mapped %d entries into %d segments\n", NSG, nents);
	/* ... device writes; then hand buffers back to the CPU ... */
	dma_sync_sg_for_cpu(dev, sg, NSG, DMA_FROM_DEVICE);
	dma_unmap_sg(dev, sg, NSG, DMA_FROM_DEVICE);
free:
	for (i = 0; i < NSG; i++)
		kfree(bufs[i]);
	return ret;
}

static struct platform_driver ds_driver = {
	.probe = ds_probe,
	.driver = { .name = "training-dma-stream" },
};

static int __init ds_init(void)
{
	int ret = platform_driver_register(&ds_driver);

	if (ret)
		return ret;
	pdev_self = platform_device_register_simple("training-dma-stream", -1, NULL, 0);
	if (IS_ERR(pdev_self)) {
		platform_driver_unregister(&ds_driver);
		return PTR_ERR(pdev_self);
	}
	return 0;
}

static void __exit ds_exit(void)
{
	platform_device_unregister(pdev_self);
	platform_driver_unregister(&ds_driver);
}

module_init(ds_init);
module_exit(ds_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Streaming DMA example");
