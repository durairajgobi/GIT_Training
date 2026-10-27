// SPDX-License-Identifier: GPL-2.0
/*
 * 084_device_tree_props.c - Matching via Device Tree and reading properties.
 *
 *   sensor@0 {
 *       compatible = "training,dt-sensor-v2";
 *       label = "board-temp";
 *       sample-rate-hz = <500>;
 *       thresholds = <10 50 90>;
 *       low-power;
 *   };
 *
 * Uses the generic device_property_* API, so it also works with ACPI _DSD.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>

struct chip_info {
	const char *name;
	int max_rate;
};

static const struct chip_info v1 = { "v1", 100 };
static const struct chip_info v2 = { "v2", 1000 };

static int dt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct chip_info *info = device_get_match_data(dev);
	const char *label = "unnamed";
	u32 rate = 100, th[3] = { };
	int n;

	if (!info)
		return -ENODEV;

	device_property_read_string(dev, "label", &label);
	device_property_read_u32(dev, "sample-rate-hz", &rate);
	if (rate > info->max_rate)
		return dev_err_probe(dev, -EINVAL, "rate %u > max %d\n", rate, info->max_rate);

	n = device_property_count_u32(dev, "thresholds");
	if (n == ARRAY_SIZE(th))
		device_property_read_u32_array(dev, "thresholds", th, n);

	dev_info(dev, "%s chip '%s' rate=%u thresholds=%u/%u/%u low-power=%d\n",
		 info->name, label, rate, th[0], th[1], th[2],
		 device_property_read_bool(dev, "low-power"));
	return 0;
}

static const struct of_device_id dt_ids[] = {
	{ .compatible = "training,dt-sensor-v1", .data = &v1 },
	{ .compatible = "training,dt-sensor-v2", .data = &v2 },
	{ }
};
MODULE_DEVICE_TABLE(of, dt_ids);

static struct platform_driver dt_driver = {
	.probe = dt_probe,
	.driver = {
		.name = "training-dt-sensor",
		.of_match_table = dt_ids,
	},
};
module_platform_driver(dt_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Device tree matching and properties example");
