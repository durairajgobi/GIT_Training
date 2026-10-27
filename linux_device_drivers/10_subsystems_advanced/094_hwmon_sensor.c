// SPDX-License-Identifier: GPL-2.0
/*
 * 094_hwmon_sensor.c - A virtual temperature/fan sensor using the hwmon API.
 *
 * sensors                   # lm-sensors shows "training_hwmon"
 * cat /sys/class/hwmon/hwmonN/temp1_input
 */
#include <linux/module.h>
#include <linux/hwmon.h>
#include <linux/platform_device.h>
#include <linux/random.h>

struct vsensor {
	long temp_max;	/* millidegrees C */
};

static struct platform_device *pdev_self;

static umode_t vs_is_visible(const void *data, enum hwmon_sensor_types type,
			     u32 attr, int channel)
{
	if (type == hwmon_temp && attr == hwmon_temp_max)
		return 0644;
	return 0444;
}

static int vs_read(struct device *dev, enum hwmon_sensor_types type,
		   u32 attr, int channel, long *val)
{
	struct vsensor *s = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		if (attr == hwmon_temp_input)
			*val = 40000 + get_random_u32_below(5000);
		else if (attr == hwmon_temp_max)
			*val = s->temp_max;
		else
			return -EOPNOTSUPP;
		return 0;
	case hwmon_fan:
		if (attr != hwmon_fan_input)
			return -EOPNOTSUPP;
		*val = 2000 + get_random_u32_below(200);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int vs_write(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, long val)
{
	struct vsensor *s = dev_get_drvdata(dev);

	if (type != hwmon_temp || attr != hwmon_temp_max)
		return -EOPNOTSUPP;
	s->temp_max = clamp_val(val, 0, 125000);
	return 0;
}

static const struct hwmon_channel_info * const vs_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MAX),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	NULL
};

static const struct hwmon_ops vs_ops = {
	.is_visible = vs_is_visible,
	.read       = vs_read,
	.write      = vs_write,
};

static const struct hwmon_chip_info vs_chip = {
	.ops  = &vs_ops,
	.info = vs_info,
};

static int vs_probe(struct platform_device *pdev)
{
	struct vsensor *s;
	struct device *hw;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->temp_max = 85000;

	hw = devm_hwmon_device_register_with_info(&pdev->dev, "training_hwmon",
						  s, &vs_chip, NULL);
	return PTR_ERR_OR_ZERO(hw);
}

static struct platform_driver vs_driver = {
	.probe = vs_probe,
	.driver = { .name = "training-hwmon" },
};

static int __init vs_init(void)
{
	int ret = platform_driver_register(&vs_driver);

	if (ret)
		return ret;
	pdev_self = platform_device_register_simple("training-hwmon", -1, NULL, 0);
	if (IS_ERR(pdev_self)) {
		platform_driver_unregister(&vs_driver);
		return PTR_ERR(pdev_self);
	}
	return 0;
}

static void __exit vs_exit(void)
{
	platform_device_unregister(pdev_self);
	platform_driver_unregister(&vs_driver);
}

module_init(vs_init);
module_exit(vs_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("hwmon sensor example");
