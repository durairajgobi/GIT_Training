// SPDX-License-Identifier: GPL-2.0
/*
 * 013_cdev_basic.c - struct cdev with open/release/read/write callbacks.
 *
 * Create the node by hand after loading:
 *   sudo mknod /dev/cdev_basic c $(awk '$2=="cdev_basic"{print $1}' /proc/devices) 0
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#define DEV_NAME "cdev_basic"

static dev_t devno;
static struct cdev my_cdev;

static int cb_open(struct inode *inode, struct file *f)
{
	pr_info("cdev: open (minor %d)\n", iminor(inode));
	return 0;
}

static int cb_release(struct inode *inode, struct file *f)
{
	pr_info("cdev: release\n");
	return 0;
}

static ssize_t cb_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	pr_info("cdev: read %zu bytes requested\n", len);
	return 0;	/* EOF */
}

static ssize_t cb_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	pr_info("cdev: write %zu bytes (discarded)\n", len);
	return len;
}

static const struct file_operations cb_fops = {
	.owner   = THIS_MODULE,
	.open    = cb_open,
	.release = cb_release,
	.read    = cb_read,
	.write   = cb_write,
};

static int __init cb_init(void)
{
	int ret = alloc_chrdev_region(&devno, 0, 1, DEV_NAME);

	if (ret)
		return ret;

	cdev_init(&my_cdev, &cb_fops);
	ret = cdev_add(&my_cdev, devno, 1);
	if (ret) {
		unregister_chrdev_region(devno, 1);
		return ret;
	}
	pr_info("cdev: major %d ready\n", MAJOR(devno));
	return 0;
}

static void __exit cb_exit(void)
{
	cdev_del(&my_cdev);
	unregister_chrdev_region(devno, 1);
}

module_init(cb_init);
module_exit(cb_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Basic cdev example");
