// SPDX-License-Identifier: GPL-2.0
/*
 * 007_jiffies_hz.c - Measuring time with jiffies and HZ.
 *
 * Prints how long the module stayed loaded when it is removed.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/jiffies.h>

static unsigned long load_jiffies;

static int __init jif_init(void)
{
	load_jiffies = jiffies;
	pr_info("jiffies: HZ=%d, jiffies=%lu\n", HZ, load_jiffies);
	pr_info("jiffies: 1500 ms = %lu jiffies\n", msecs_to_jiffies(1500));
	return 0;
}

static void __exit jif_exit(void)
{
	unsigned long delta = jiffies - load_jiffies;

	pr_info("jiffies: loaded for %lu jiffies (%u ms)\n",
		delta, jiffies_to_msecs(delta));

	if (time_after(jiffies, load_jiffies + 10 * HZ))
		pr_info("jiffies: module was loaded more than 10 seconds\n");
}

module_init(jif_init);
module_exit(jif_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("jiffies and HZ example");
