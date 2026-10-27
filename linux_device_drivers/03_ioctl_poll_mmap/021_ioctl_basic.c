// SPDX-License-Identifier: GPL-2.0
/*
 * 021_ioctl_basic.c - Simple ioctl commands to get/set an integer.
 *
 * User space:
 *   #define IOC_MAGIC 'T'
 *   #define IOC_SET_VAL _IOW(IOC_MAGIC, 1, int)
 *   #define IOC_GET_VAL _IOR(IOC_MAGIC, 2, int)
 *   #define IOC_RESET   _IO(IOC_MAGIC, 3)
 *   ioctl(fd, IOC_SET_VAL, &v); ioctl(fd, IOC_GET_VAL, &v);
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>

#define IOC_MAGIC   'T'
#define IOC_SET_VAL _IOW(IOC_MAGIC, 1, int)
#define IOC_GET_VAL _IOR(IOC_MAGIC, 2, int)
#define IOC_RESET   _IO(IOC_MAGIC, 3)

static int value;

static long ib_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int __user *uptr = (int __user *)arg;

	if (_IOC_TYPE(cmd) != IOC_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case IOC_SET_VAL:
		if (get_user(value, uptr))
			return -EFAULT;
		pr_info("ioctl: value set to %d\n", value);
		return 0;
	case IOC_GET_VAL:
		return put_user(value, uptr);
	case IOC_RESET:
		value = 0;
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations ib_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = ib_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

static struct miscdevice ib_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "ioctl_basic",
	.fops  = &ib_fops,
	.mode  = 0666,
};

module_misc_device(ib_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Basic ioctl example");
