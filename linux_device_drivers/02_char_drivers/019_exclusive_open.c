// SPDX-License-Identifier: GPL-2.0
/*
 * 019_exclusive_open.c - Allow only one process to open the device at a time.
 *
 * A second open() fails with -EBUSY until the first one closes.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/atomic.h>

static atomic_t in_use = ATOMIC_INIT(0);

static int ex_open(struct inode *inode, struct file *f)
{
	if (atomic_cmpxchg(&in_use, 0, 1) != 0) {
		pr_info("excl: device busy\n");
		return -EBUSY;
	}
	pr_info("excl: opened by pid %d\n", current->pid);
	return 0;
}

static int ex_release(struct inode *inode, struct file *f)
{
	atomic_set(&in_use, 0);
	pr_info("excl: released\n");
	return 0;
}

static const struct file_operations ex_fops = {
	.owner   = THIS_MODULE,
	.open    = ex_open,
	.release = ex_release,
};

static struct miscdevice ex_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "exclusive",
	.fops  = &ex_fops,
	.mode  = 0666,
};

module_misc_device(ex_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Single-open device example");
