// SPDX-License-Identifier: GPL-2.0
/*
 * 081_platform_driver.c - Minimal platform driver with probe/remove and PM ops.
 *
 * Binds by name to a platform device called "training-pdev" (see 082).
 * ls /sys/bus/platform/drivers/training-pdev/
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/mod_devicetable.h>
#include <linux/version.h>

static int pd_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "probe (id=%d)\n", pdev->id);
	return 0;
}

static void pd_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "remove\n");
}

static int pd_suspend(struct device *dev)
{
	dev_info(dev, "suspend: save state, gate clocks\n");
	return 0;
}

static int pd_resume(struct device *dev)
{
	dev_info(dev, "resume: restore state\n");
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pd_pm_ops, pd_suspend, pd_resume);

static const struct platform_device_id pd_ids[] = {
	{ "training-pdev", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, pd_ids);

static struct platform_driver pd_driver = {
	.probe    = pd_probe,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	.remove   = pd_remove,
#else
	.remove_new = pd_remove,
#endif
	.id_table = pd_ids,
	.driver = {
		.name = "training-pdev",
		.pm   = pm_sleep_ptr(&pd_pm_ops),
	},
};
module_platform_driver(pd_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal platform driver");
