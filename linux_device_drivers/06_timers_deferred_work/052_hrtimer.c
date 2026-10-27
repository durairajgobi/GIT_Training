// SPDX-License-Identifier: GPL-2.0
/*
 * 052_hrtimer.c - High-resolution timer with nanosecond precision.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

static unsigned long period_us = 100000;	/* 100 ms */
module_param(period_us, ulong, 0444);

static struct hrtimer hrt;
static ktime_t period, last;
static unsigned int fires;

static enum hrtimer_restart hrt_cb(struct hrtimer *t)
{
	ktime_t now = ktime_get();

	if (fires++ % 10 == 0)
		pr_info("hrtimer: fire %u, delta=%lld ns\n",
			fires, ktime_to_ns(ktime_sub(now, last)));
	last = now;
	hrtimer_forward_now(t, period);
	return HRTIMER_RESTART;
}

static int __init hrt_init(void)
{
	period = ktime_set(0, period_us * NSEC_PER_USEC);
	hrtimer_init(&hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrt.function = hrt_cb;
	last = ktime_get();
	hrtimer_start(&hrt, period, HRTIMER_MODE_REL);
	return 0;
}

static void __exit hrt_exit(void)
{
	hrtimer_cancel(&hrt);
	pr_info("hrtimer: %u fires\n", fires);
}

module_init(hrt_init);
module_exit(hrt_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("High resolution timer example");
