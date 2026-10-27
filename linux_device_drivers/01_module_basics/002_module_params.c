// SPDX-License-Identifier: GPL-2.0
/*
 * 002_module_params.c - Passing parameters to a module at load time.
 *
 * sudo insmod 002_module_params.ko count=3 name="driver" verbose=1
 * cat /sys/module/002_module_params/parameters/count
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

static int count = 1;
static char *name = "world";
static bool verbose;

module_param(count, int, 0644);
MODULE_PARM_DESC(count, "Number of greetings to print");
module_param(name, charp, 0444);
MODULE_PARM_DESC(name, "Name to greet");
module_param(verbose, bool, 0644);
MODULE_PARM_DESC(verbose, "Enable verbose output");

static int __init params_init(void)
{
	int i;

	if (count < 0 || count > 10) {
		pr_err("params: count must be 0..10\n");
		return -EINVAL;
	}

	for (i = 0; i < count; i++)
		pr_info("params: hello %s (%d)\n", name, i);

	if (verbose)
		pr_info("params: verbose mode enabled\n");
	return 0;
}

static void __exit params_exit(void)
{
	pr_info("params: goodbye %s\n", name);
}

module_init(params_init);
module_exit(params_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module parameter example");
