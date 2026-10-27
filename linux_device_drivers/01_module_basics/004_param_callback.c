// SPDX-License-Identifier: GPL-2.0
/*
 * 004_param_callback.c - Get notified when a parameter changes at runtime.
 *
 * echo 42 | sudo tee /sys/module/004_param_callback/parameters/level
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

static int level = 1;

static int level_set(const char *val, const struct kernel_param *kp)
{
	int old = level, ret;

	ret = param_set_int(val, kp);
	if (ret)
		return ret;

	if (level < 0 || level > 100) {
		level = old;
		return -EINVAL;
	}
	pr_info("pcb: level changed %d -> %d\n", old, level);
	return 0;
}

static const struct kernel_param_ops level_ops = {
	.set = level_set,
	.get = param_get_int,
};

module_param_cb(level, &level_ops, &level, 0644);
MODULE_PARM_DESC(level, "Level 0..100, change notified via callback");

static int __init pcb_init(void)
{
	pr_info("pcb: loaded with level=%d\n", level);
	return 0;
}

static void __exit pcb_exit(void)
{
	pr_info("pcb: unloaded\n");
}

module_init(pcb_init);
module_exit(pcb_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module parameter callback example");
