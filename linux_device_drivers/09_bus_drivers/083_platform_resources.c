// SPDX-License-Identifier: GPL-2.0
/*
 * 083_platform_resources.c - Passing MEM/IRQ resources and platform data.
 *
 * One module registers both a device (with resources) and the driver
 * that consumes them, so it is self-contained.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>

struct sensor_pdata {
	u32 sample_rate;
};

static struct resource res[] = {
	DEFINE_RES_MEM_NAMED(0xfe000000, 0x1000, "regs"),
	DEFINE_RES_MEM_NAMED(0xfe010000, 0x100, "fifo"),
};

static const struct sensor_pdata pdata = { .sample_rate = 1000 };
static struct platform_device *pdev_self;

static int pr_probe(struct platform_device *pdev)
{
	const struct sensor_pdata *pd = dev_get_platdata(&pdev->dev);
	struct resource *r;
	int i;

	if (!pd)
		return -EINVAL;

	for (i = 0; i < pdev->num_resources; i++) {
		r = &pdev->resource[i];
		dev_info(&pdev->dev, "resource %d: %pR\n", i, r);
	}

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fifo");
	if (r)
		dev_info(&pdev->dev, "fifo window size %llu\n",
			 (unsigned long long)resource_size(r));

	dev_info(&pdev->dev, "sample rate %u Hz\n", pd->sample_rate);
	/* a real driver would devm_ioremap_resource() here (see 079) */
	return 0;
}

static struct platform_driver pr_driver = {
	.probe = pr_probe,
	.driver = { .name = "training-res" },
};

static int __init pr_init(void)
{
	struct platform_device_info info = {
		.name      = "training-res",
		.id        = PLATFORM_DEVID_NONE,
		.res       = res,
		.num_res   = ARRAY_SIZE(res),
		.data      = &pdata,
		.size_data = sizeof(pdata),
	};
	int ret = platform_driver_register(&pr_driver);

	if (ret)
		return ret;
	pdev_self = platform_device_register_full(&info);
	if (IS_ERR(pdev_self)) {
		platform_driver_unregister(&pr_driver);
		return PTR_ERR(pdev_self);
	}
	return 0;
}

static void __exit pr_exit(void)
{
	platform_device_unregister(pdev_self);
	platform_driver_unregister(&pr_driver);
}

module_init(pr_init);
module_exit(pr_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Platform resources example");
