// SPDX-License-Identifier: GPL-2.0
/*
 * 010_module_refcount.c - Holding a module reference.
 *
 * While "hold" is 1 the module cannot be removed (rmmod reports "in use").
 *   echo 1 | sudo tee /sys/module/010_module_refcount/parameters/hold
 *   lsmod | grep 010_module_refcount
 *   echo 0 | sudo tee /sys/module/010_module_refcount/parameters/hold
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

static bool held;

static int hold_set(const char *val, const struct kernel_param *kp)
{
	bool want;
	int ret = kstrtobool(val, &want);

	if (ret)
		return ret;

	if (want && !held) {
		if (!try_module_get(THIS_MODULE))
			return -ENODEV;
		held = true;
	} else if (!want && held) {
		module_put(THIS_MODULE);
		held = false;
	}
	pr_info("refcnt: refcount now %d\n", module_refcount(THIS_MODULE));
	return 0;
}

static int hold_get(char *buf, const struct kernel_param *kp)
{
	return sprintf(buf, "%d\n", held);
}

static const struct kernel_param_ops hold_ops = {
	.set = hold_set,
	.get = hold_get,
};
module_param_cb(hold, &hold_ops, NULL, 0644);
MODULE_PARM_DESC(hold, "1 = take a module reference, 0 = drop it");

static int __init refcnt_init(void)
{
	pr_info("refcnt: loaded, refcount %d\n", module_refcount(THIS_MODULE));
	return 0;
}

static void __exit refcnt_exit(void)
{
	pr_info("refcnt: unloaded\n");
}

module_init(refcnt_init);
module_exit(refcnt_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module reference counting example");
