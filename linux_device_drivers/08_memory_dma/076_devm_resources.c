// SPDX-License-Identifier: GPL-2.0
/*
 * 076_devm_resources.c - Device-managed (devm_*) resources.
 *
 * Everything allocated with devm_* is released automatically when the
 * device is unbound, so probe() needs no unwind path and remove() is empty.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/device.h>

struct priv {
	char *name;
	u32 *table;
};

static struct platform_device *pdev_self;

static void custom_cleanup(void *data)
{
	pr_info("devm: custom action ran for %s\n", (char *)data);
}

static int dm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct priv *p;
	int ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->name = devm_kasprintf(dev, GFP_KERNEL, "%s-instance", dev_name(dev));
	p->table = devm_kcalloc(dev, 64, sizeof(u32), GFP_KERNEL);
	if (!p->name || !p->table)
		return -ENOMEM;

	/* register arbitrary cleanup to run at unbind, in reverse order */
	ret = devm_add_action_or_reset(dev, custom_cleanup, p->name);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, p);
	dev_info(dev, "probed %s\n", p->name);
	return 0;
}

static struct platform_driver dm_driver = {
	.probe = dm_probe,
	.driver = { .name = "training-devm" },
};

static int __init dm_init(void)
{
	int ret = platform_driver_register(&dm_driver);

	if (ret)
		return ret;
	pdev_self = platform_device_register_simple("training-devm", 0, NULL, 0);
	if (IS_ERR(pdev_self)) {
		platform_driver_unregister(&dm_driver);
		return PTR_ERR(pdev_self);
	}
	return 0;
}

static void __exit dm_exit(void)
{
	platform_device_unregister(pdev_self);
	platform_driver_unregister(&dm_driver);
}

module_init(dm_init);
module_exit(dm_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("devm managed resources example");
