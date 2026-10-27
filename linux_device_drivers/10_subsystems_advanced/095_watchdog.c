// SPDX-License-Identifier: GPL-2.0
/*
 * 095_watchdog.c - A software watchdog using the watchdog core.
 *
 * Opening /dev/watchdogN starts it; it must be pinged (any write) before
 * the timeout or the module logs an emergency (a real one would reset).
 * Write 'V' before close for "magic close" to stop it cleanly.
 */
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/timer.h>

struct swdog {
	struct watchdog_device wdd;
	struct timer_list timer;
};

static struct platform_device *pdev_self;

static void swdog_expired(struct timer_list *t)
{
	pr_emerg("swdog: watchdog expired! (real hardware would reset now)\n");
}

static int swdog_start(struct watchdog_device *wdd)
{
	struct swdog *s = watchdog_get_drvdata(wdd);

	mod_timer(&s->timer, jiffies + wdd->timeout * HZ);
	return 0;
}

static int swdog_stop(struct watchdog_device *wdd)
{
	struct swdog *s = watchdog_get_drvdata(wdd);

	del_timer_sync(&s->timer);
	return 0;
}

static int swdog_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	wdd->timeout = t;
	if (watchdog_active(wdd))
		swdog_start(wdd);
	return 0;
}

static const struct watchdog_info swdog_info = {
	.identity = "Training Soft Watchdog",
	.options  = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops swdog_ops = {
	.owner       = THIS_MODULE,
	.start       = swdog_start,
	.stop        = swdog_stop,
	.ping        = swdog_start,	/* re-arm == ping */
	.set_timeout = swdog_set_timeout,
};

static int swdog_probe(struct platform_device *pdev)
{
	struct swdog *s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);

	if (!s)
		return -ENOMEM;
	timer_setup(&s->timer, swdog_expired, 0);

	s->wdd.info        = &swdog_info;
	s->wdd.ops         = &swdog_ops;
	s->wdd.parent      = &pdev->dev;
	s->wdd.min_timeout = 1;
	s->wdd.max_timeout = 300;
	s->wdd.timeout     = 30;
	watchdog_set_drvdata(&s->wdd, s);
	watchdog_stop_on_unregister(&s->wdd);
	platform_set_drvdata(pdev, s);

	return devm_watchdog_register_device(&pdev->dev, &s->wdd);
}

static struct platform_driver swdog_driver = {
	.probe = swdog_probe,
	.driver = { .name = "training-swdog" },
};

static int __init swdog_init(void)
{
	int ret = platform_driver_register(&swdog_driver);

	if (ret)
		return ret;
	pdev_self = platform_device_register_simple("training-swdog", -1, NULL, 0);
	if (IS_ERR(pdev_self)) {
		platform_driver_unregister(&swdog_driver);
		return PTR_ERR(pdev_self);
	}
	return 0;
}

static void __exit swdog_exit(void)
{
	platform_device_unregister(pdev_self);
	platform_driver_unregister(&swdog_driver);
}

module_init(swdog_init);
module_exit(swdog_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Software watchdog example");
