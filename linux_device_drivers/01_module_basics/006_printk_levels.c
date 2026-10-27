// SPDX-License-Identifier: GPL-2.0
/*
 * 006_printk_levels.c - Log levels, pr_fmt, rate limiting and print-once.
 *
 * Use "dmesg -l debug" or dynamic debug to see pr_debug output:
 *   echo 'module 006_printk_levels +p' > /sys/kernel/debug/dynamic_debug/control
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init plevel_init(void)
{
	int i;

	pr_emerg("emergency level\n");
	pr_alert("alert level\n");
	pr_crit("critical level\n");
	pr_err("error level\n");
	pr_warn("warning level\n");
	pr_notice("notice level\n");
	pr_info("info level\n");
	pr_debug("debug level (needs DEBUG or dynamic debug)\n");

	for (i = 0; i < 100; i++) {
		pr_info_ratelimited("rate limited message %d\n", i);
		pr_info_once("printed only once\n");
	}

	printk(KERN_INFO "classic printk with explicit level\n");
	return 0;
}

static void __exit plevel_exit(void)
{
	pr_info("exit\n");
}

module_init(plevel_init);
module_exit(plevel_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("printk log level example");
