// SPDX-License-Identifier: GPL-2.0
/*
 * 082_platform_device.c - Registering platform devices from code (board file style).
 *
 * Load 081 and then this module; 081's probe() runs for each device.
 * Platform data passes board-specific configuration to the driver.
 */
#include <linux/module.h>
#include <linux/platform_device.h>

struct training_pdata {
	int num_channels;
	const char *label;
};

static struct training_pdata pdata0 = { .num_channels = 2, .label = "first" };
static struct training_pdata pdata1 = { .num_channels = 8, .label = "second" };

static struct platform_device *pdevs[2];

static int __init pdev_init(void)
{
	struct training_pdata *pd[] = { &pdata0, &pdata1 };
	int i;

	for (i = 0; i < 2; i++) {
		pdevs[i] = platform_device_register_data(NULL, "training-pdev", i,
							 pd[i], sizeof(*pd[i]));
		if (IS_ERR(pdevs[i])) {
			int ret = PTR_ERR(pdevs[i]);

			while (--i >= 0)
				platform_device_unregister(pdevs[i]);
			return ret;
		}
	}
	pr_info("pdev: registered 2 training-pdev devices\n");
	return 0;
}

static void __exit pdev_exit(void)
{
	platform_device_unregister(pdevs[1]);
	platform_device_unregister(pdevs[0]);
}

module_init(pdev_init);
module_exit(pdev_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Platform device registration example");
