// SPDX-License-Identifier: GPL-2.0
/*
 * 055_delayed_work.c - Periodic polling with delayed_work.
 *
 * A common pattern for drivers that must poll hardware status (e.g. a
 * link state or sensor) without an interrupt.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/random.h>

static unsigned int poll_ms = 2000;
module_param(poll_ms, uint, 0444);

static struct delayed_work poll_work;
static bool link_up;

static void poll_fn(struct work_struct *w)
{
	bool now = get_random_u32() & 1;	/* pretend to read a status reg */

	if (now != link_up) {
		link_up = now;
		pr_info("dwork: link is %s\n", link_up ? "UP" : "DOWN");
	}
	schedule_delayed_work(&poll_work, msecs_to_jiffies(poll_ms));
}

static int __init dw_init(void)
{
	INIT_DELAYED_WORK(&poll_work, poll_fn);
	schedule_delayed_work(&poll_work, 0);
	return 0;
}

static void __exit dw_exit(void)
{
	cancel_delayed_work_sync(&poll_work);
}

module_init(dw_init);
module_exit(dw_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Delayed work polling example");
