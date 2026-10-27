// SPDX-License-Identifier: GPL-2.0
/*
 * 023_ioctl_capability.c - Restricting privileged ioctls with capable().
 *
 * Any user may read the counter; only CAP_SYS_ADMIN may clear it.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/capability.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>

#define CAP_MAGIC   'K'
#define CAP_GET_CNT _IOR(CAP_MAGIC, 1, int)
#define CAP_CLR_CNT _IO(CAP_MAGIC, 2)

static atomic_t counter = ATOMIC_INIT(0);

static int ic_open(struct inode *inode, struct file *f)
{
	atomic_inc(&counter);
	return 0;
}

static long ic_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case CAP_GET_CNT:
		return put_user(atomic_read(&counter), (int __user *)arg);
	case CAP_CLR_CNT:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		atomic_set(&counter, 0);
		return 0;
	}
	return -ENOTTY;
}

static const struct file_operations ic_fops = {
	.owner          = THIS_MODULE,
	.open           = ic_open,
	.unlocked_ioctl = ic_ioctl,
};

static struct miscdevice ic_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "ioctl_cap",
	.fops  = &ic_fops,
	.mode  = 0666,
};

module_misc_device(ic_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ioctl permission checks example");
