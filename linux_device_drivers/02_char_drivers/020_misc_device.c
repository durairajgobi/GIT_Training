// SPDX-License-Identifier: GPL-2.0
/*
 * 020_misc_device.c - Misc device with explicit init/exit.
 *
 * Misc devices share major 10 and get a dynamic minor plus an automatic
 * /dev entry - the simplest way to expose a char device.
 * cat /dev/training_misc
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>

static ssize_t misc_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	char tmp[64];
	int n = scnprintf(tmp, sizeof(tmp), "read by %s (pid %d)\n",
			  current->comm, current->pid);

	return simple_read_from_buffer(buf, len, off, tmp, n);
}

static const struct file_operations misc_fops = {
	.owner = THIS_MODULE,
	.read  = misc_read,
};

static struct miscdevice training_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "training_misc",
	.fops  = &misc_fops,
	.mode  = 0444,
};

static int __init misc_init(void)
{
	int ret = misc_register(&training_misc);

	if (ret)
		return ret;
	pr_info("misc: registered minor %d\n", training_misc.minor);
	return 0;
}

static void __exit misc_exit(void)
{
	misc_deregister(&training_misc);
}

module_init(misc_init);
module_exit(misc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Misc device example");
