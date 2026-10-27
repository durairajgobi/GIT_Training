// SPDX-License-Identifier: GPL-2.0
/*
 * 065_gpio_button_irq.c - Push button on a GPIO with debounce and IRQ.
 *
 * Device tree:
 *   button {
 *       compatible = "training,gpio-button";
 *       button-gpios = <&gpio 17 GPIO_ACTIVE_LOW>;
 *   };
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of.h>

struct btn {
	struct gpio_desc *gpiod;
	int irq;
	unsigned int presses;
	struct device *dev;
};

static irqreturn_t btn_thread(int irq, void *data)
{
	struct btn *b = data;
	int val = gpiod_get_value_cansleep(b->gpiod);	/* logical, honors ACTIVE_LOW */

	if (val)
		b->presses++;
	dev_info(b->dev, "button %s (presses=%u)\n", val ? "pressed" : "released",
		 b->presses);
	return IRQ_HANDLED;
}

static int btn_probe(struct platform_device *pdev)
{
	struct btn *b;
	int ret;

	b = devm_kzalloc(&pdev->dev, sizeof(*b), GFP_KERNEL);
	if (!b)
		return -ENOMEM;
	b->dev = &pdev->dev;

	b->gpiod = devm_gpiod_get(&pdev->dev, "button", GPIOD_IN);
	if (IS_ERR(b->gpiod))
		return dev_err_probe(&pdev->dev, PTR_ERR(b->gpiod), "get gpio\n");

	/* hardware debounce if the controller supports it (20 ms) */
	if (gpiod_set_debounce(b->gpiod, 20000))
		dev_info(&pdev->dev, "no hw debounce\n");

	b->irq = gpiod_to_irq(b->gpiod);
	if (b->irq < 0)
		return b->irq;

	ret = devm_request_threaded_irq(&pdev->dev, b->irq, NULL, btn_thread,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT, "gpio-button", b);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, b);
	return 0;
}

static const struct of_device_id btn_of_match[] = {
	{ .compatible = "training,gpio-button" },
	{ }
};
MODULE_DEVICE_TABLE(of, btn_of_match);

static struct platform_driver btn_driver = {
	.probe = btn_probe,
	.driver = {
		.name = "training-gpio-button",
		.of_match_table = btn_of_match,
	},
};
module_platform_driver(btn_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIO button interrupt example");
