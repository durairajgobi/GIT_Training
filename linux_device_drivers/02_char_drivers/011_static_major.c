// SPDX-License-Identifier: GPL-2.0
/*
 * 011_static_major.c - Old-style registration with a fixed major number.
 *
 * sudo insmod 011_static_major.ko major=240
 * sudo mknod /dev/static_chr c 240 0
 * cat /dev/static_chr
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define DEV_NAME "static_chr"

static int major = 240;
module_param(major, int, 0444);

static const char msg[] = "Hello from a static major device\n";

static ssize_t st_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	return simple_read_from_buffer(buf, len, off, msg, sizeof(msg) - 1);
}

static const struct file_operations st_fops = {
	.owner = THIS_MODULE,
	.read  = st_read,
};

static int __init st_init(void)
{
	int ret = register_chrdev(major, DEV_NAME, &st_fops);

	if (ret < 0) {
		pr_err("static: cannot get major %d\n", major);
		return ret;
	}
	if (major == 0)		/* 0 asks the kernel to pick one */
		major = ret;
	pr_info("static: registered major %d\n", major);
	return 0;
}

static void __exit st_exit(void)
{
	unregister_chrdev(major, DEV_NAME);
	pr_info("static: unregistered\n");
}

module_init(st_init);
module_exit(st_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("register_chrdev with a static major");
