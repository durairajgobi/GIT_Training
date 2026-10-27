// SPDX-License-Identifier: GPL-2.0
/*
 * 014_auto_device_node.c - Let udev create /dev/auto_node via class/device.
 *
 * After insmod: ls -l /dev/auto_node /sys/class/training_class/
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/version.h>

#define DEV_NAME   "auto_node"
#define CLASS_NAME "training_class"

static dev_t devno;
static struct cdev an_cdev;
static struct class *an_class;
static struct device *an_device;

static const struct file_operations an_fops = {
	.owner = THIS_MODULE,
};

static int __init an_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&devno, 0, 1, DEV_NAME);
	if (ret)
		return ret;

	cdev_init(&an_cdev, &an_fops);
	ret = cdev_add(&an_cdev, devno, 1);
	if (ret)
		goto err_region;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	an_class = class_create(CLASS_NAME);
#else
	an_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(an_class)) {
		ret = PTR_ERR(an_class);
		goto err_cdev;
	}

	an_device = device_create(an_class, NULL, devno, NULL, DEV_NAME);
	if (IS_ERR(an_device)) {
		ret = PTR_ERR(an_device);
		goto err_class;
	}

	pr_info("auto: /dev/%s created (%d:%d)\n", DEV_NAME, MAJOR(devno), MINOR(devno));
	return 0;

err_class:
	class_destroy(an_class);
err_cdev:
	cdev_del(&an_cdev);
err_region:
	unregister_chrdev_region(devno, 1);
	return ret;
}

static void __exit an_exit(void)
{
	device_destroy(an_class, devno);
	class_destroy(an_class);
	cdev_del(&an_cdev);
	unregister_chrdev_region(devno, 1);
}

module_init(an_init);
module_exit(an_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Automatic /dev node creation with class/device");
