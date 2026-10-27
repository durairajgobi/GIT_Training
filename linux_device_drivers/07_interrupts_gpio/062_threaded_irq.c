// SPDX-License-Identifier: GPL-2.0
/*
 * 062_threaded_irq.c - Threaded interrupt handler in a platform driver.
 *
 * The hard handler only acknowledges the device; the threaded handler runs
 * in a kernel thread and may sleep (e.g. to talk to an I2C/SPI chip).
 * Binds to a platform device with an IRQ resource (from DT/ACPI):
 *   compatible = "training,threaded-irq";
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/delay.h>

struct tirq_priv {
	struct device *dev;
	int irq;
	unsigned long events;
};

static irqreturn_t tirq_hard(int irq, void *data)
{
	/* minimal work in hard-irq context: ack / mask the source */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t tirq_thread(int irq, void *data)
{
	struct tirq_priv *p = data;

	msleep(1);		/* sleeping is fine here */
	p->events++;
	dev_info(p->dev, "threaded handler, events=%lu\n", p->events);
	return IRQ_HANDLED;
}

static int tirq_probe(struct platform_device *pdev)
{
	struct tirq_priv *p;
	int ret;

	p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->dev = &pdev->dev;

	p->irq = platform_get_irq(pdev, 0);
	if (p->irq < 0)
		return p->irq;

	ret = devm_request_threaded_irq(&pdev->dev, p->irq, tirq_hard, tirq_thread,
					IRQF_ONESHOT, dev_name(&pdev->dev), p);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "request irq %d\n", p->irq);

	platform_set_drvdata(pdev, p);
	dev_info(&pdev->dev, "using irq %d\n", p->irq);
	return 0;
}

static const struct of_device_id tirq_of_match[] = {
	{ .compatible = "training,threaded-irq" },
	{ }
};
MODULE_DEVICE_TABLE(of, tirq_of_match);

static struct platform_driver tirq_driver = {
	.probe = tirq_probe,
	.driver = {
		.name = "threaded-irq-demo",
		.of_match_table = tirq_of_match,
	},
};
module_platform_driver(tirq_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Threaded IRQ example");
