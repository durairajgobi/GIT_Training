// SPDX-License-Identifier: GPL-2.0
/*
 * 066_gpio_led_blink.c - Blinking an LED on a GPIO output with a timer.
 *
 * Device tree:
 *   blinker {
 *       compatible = "training,gpio-blink";
 *       led-gpios = <&gpio 18 GPIO_ACTIVE_HIGH>;
 *   };
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/timer.h>
#include <linux/of.h>
#include <linux/version.h>

static unsigned int blink_ms = 500;
module_param(blink_ms, uint, 0644);

struct blink {
	struct gpio_desc *led;
	struct timer_list timer;
	bool on;
};

static void blink_fn(struct timer_list *t)
{
	struct blink *b = from_timer(b, t, timer);

	b->on = !b->on;
	gpiod_set_value(b->led, b->on);	/* atomic-safe for MMIO GPIO controllers */
	mod_timer(&b->timer, jiffies + msecs_to_jiffies(blink_ms));
}

static int blink_probe(struct platform_device *pdev)
{
	struct blink *b = devm_kzalloc(&pdev->dev, sizeof(*b), GFP_KERNEL);

	if (!b)
		return -ENOMEM;
	b->led = devm_gpiod_get(&pdev->dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(b->led))
		return PTR_ERR(b->led);
	if (gpiod_cansleep(b->led))
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "GPIO sleeps; use a workqueue instead of a timer\n");

	timer_setup(&b->timer, blink_fn, 0);
	mod_timer(&b->timer, jiffies + msecs_to_jiffies(blink_ms));
	platform_set_drvdata(pdev, b);
	return 0;
}

static void blink_remove(struct platform_device *pdev)
{
	struct blink *b = platform_get_drvdata(pdev);

	timer_shutdown_sync(&b->timer);
	gpiod_set_value(b->led, 0);
}

static const struct of_device_id blink_of_match[] = {
	{ .compatible = "training,gpio-blink" },
	{ }
};
MODULE_DEVICE_TABLE(of, blink_of_match);

static struct platform_driver blink_driver = {
	.probe = blink_probe,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	.remove = blink_remove,
#else
	.remove_new = blink_remove,
#endif
	.driver = {
		.name = "training-gpio-blink",
		.of_match_table = blink_of_match,
	},
};
module_platform_driver(blink_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIO LED blink example");
