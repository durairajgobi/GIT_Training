// SPDX-License-Identifier: GPL-2.0
/*
 * 090_led_class.c - Registering LEDs with the LED class (with triggers).
 *
 * ls /sys/class/leds/training:*
 * echo 255 > /sys/class/leds/training:green:status/brightness
 * echo heartbeat > /sys/class/leds/training:green:status/trigger
 */
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

struct vled {
	struct led_classdev cdev;
	enum led_brightness hw_state;	/* stands in for a register */
};

static struct platform_device *pdev_self;

static void vled_set(struct led_classdev *cdev, enum led_brightness b)
{
	struct vled *v = container_of(cdev, struct vled, cdev);

	v->hw_state = b;
	pr_debug("vled: %s -> %d\n", cdev->name, b);
}

static enum led_brightness vled_get(struct led_classdev *cdev)
{
	return container_of(cdev, struct vled, cdev)->hw_state;
}

static int vled_probe(struct platform_device *pdev)
{
	static const char * const names[] = {
		"training:green:status", "training:red:error",
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		struct vled *v = devm_kzalloc(&pdev->dev, sizeof(*v), GFP_KERNEL);

		if (!v)
			return -ENOMEM;
		v->cdev.name = names[i];
		v->cdev.max_brightness = LED_FULL;
		v->cdev.brightness_set = vled_set;	/* must not sleep */
		v->cdev.brightness_get = vled_get;
		v->cdev.default_trigger = i == 0 ? "heartbeat" : "none";

		ret = devm_led_classdev_register(&pdev->dev, &v->cdev);
		if (ret)
			return ret;
	}
	return 0;
}

static struct platform_driver vled_driver = {
	.probe = vled_probe,
	.driver = { .name = "training-vled" },
};

static int __init vled_init(void)
{
	int ret = platform_driver_register(&vled_driver);

	if (ret)
		return ret;
	pdev_self = platform_device_register_simple("training-vled", -1, NULL, 0);
	if (IS_ERR(pdev_self)) {
		platform_driver_unregister(&vled_driver);
		return PTR_ERR(pdev_self);
	}
	return 0;
}

static void __exit vled_exit(void)
{
	platform_device_unregister(pdev_self);
	platform_driver_unregister(&vled_driver);
}

module_init(vled_init);
module_exit(vled_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LED class driver example");
