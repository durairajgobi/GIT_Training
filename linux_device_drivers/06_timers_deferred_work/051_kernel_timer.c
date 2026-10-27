// SPDX-License-Identifier: GPL-2.0
/*
 * 051_kernel_timer.c - A periodic timer_list timer.
 *
 * sudo insmod 051_kernel_timer.ko period_ms=500
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

static unsigned int period_ms = 1000;
module_param(period_ms, uint, 0444);

struct my_timer {
	struct timer_list timer;
	unsigned int count;
};
static struct my_timer mt;

static void timer_cb(struct timer_list *t)
{
	struct my_timer *m = from_timer(m, t, timer);

	m->count++;
	pr_info("timer: fired %u times (in_interrupt=%d)\n",
		m->count, !!in_interrupt());
	mod_timer(&m->timer, jiffies + msecs_to_jiffies(period_ms));
}

static int __init tm_init(void)
{
	timer_setup(&mt.timer, timer_cb, 0);
	mod_timer(&mt.timer, jiffies + msecs_to_jiffies(period_ms));
	return 0;
}

static void __exit tm_exit(void)
{
	/* shutdown also prevents the callback from re-arming itself */
	timer_shutdown_sync(&mt.timer);
	pr_info("timer: stopped after %u ticks\n", mt.count);
}

module_init(tm_init);
module_exit(tm_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel timer example");
