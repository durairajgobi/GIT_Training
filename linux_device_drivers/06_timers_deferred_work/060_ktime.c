// SPDX-License-Identifier: GPL-2.0
/*
 * 060_ktime.c - Reading the different kernel clocks.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/rtc.h>

static int __init kt_init(void)
{
	struct timespec64 ts;
	struct rtc_time tm;

	pr_info("ktime: monotonic  = %lld ns\n", ktime_get_ns());
	pr_info("ktime: boottime   = %lld ns\n", ktime_get_boottime_ns());
	pr_info("ktime: raw        = %lld ns\n", ktime_get_raw_ns());
	pr_info("ktime: real (UTC) = %lld s\n", ktime_get_real_seconds());

	ktime_get_real_ts64(&ts);
	rtc_time64_to_tm(ts.tv_sec, &tm);
	pr_info("ktime: %ptRs UTC\n", &tm);
	return 0;
}

static void __exit kt_exit(void)
{
}

module_init(kt_init);
module_exit(kt_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel time API example");
