// SPDX-License-Identifier: GPL-2.0
/*
 * 012_dynamic_major.c - Dynamically allocated device numbers.
 *
 * cat /proc/devices | grep dyn_chr
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>

#define DEV_NAME  "dyn_chr"
#define DEV_COUNT 4

static dev_t devno;

static int __init dyn_init(void)
{
	int ret = alloc_chrdev_region(&devno, 0, DEV_COUNT, DEV_NAME);

	if (ret) {
		pr_err("dyn: alloc_chrdev_region failed %d\n", ret);
		return ret;
	}
	pr_info("dyn: major %d, minors %d..%d\n",
		MAJOR(devno), MINOR(devno), MINOR(devno) + DEV_COUNT - 1);
	return 0;
}

static void __exit dyn_exit(void)
{
	unregister_chrdev_region(devno, DEV_COUNT);
	pr_info("dyn: released\n");
}

module_init(dyn_init);
module_exit(dyn_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("alloc_chrdev_region example");
