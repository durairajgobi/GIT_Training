// SPDX-License-Identifier: GPL-2.0
/*
 * 059_delays.c - Busy-wait vs sleeping delays and how long they really take.
 *
 * udelay/ndelay/mdelay spin the CPU (usable in atomic context);
 * usleep_range/msleep sleep (process context only).
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ktime.h>

#define MEASURE(label, stmt) do {					\
	ktime_t t0 = ktime_get();					\
	stmt;								\
	pr_info("delays: %-22s took %7lld us\n", label,			\
		ktime_us_delta(ktime_get(), t0));			\
} while (0)

static int __init dl_init(void)
{
	MEASURE("ndelay(500)", ndelay(500));
	MEASURE("udelay(100)", udelay(100));
	MEASURE("mdelay(2)", mdelay(2));
	MEASURE("usleep_range(500,1000)", usleep_range(500, 1000));
	MEASURE("msleep(10)", msleep(10));
	MEASURE("fsleep(2000)", fsleep(2000));
	return 0;
}

static void __exit dl_exit(void)
{
}

module_init(dl_init);
module_exit(dl_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel delay functions example");
