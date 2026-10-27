// SPDX-License-Identifier: GPL-2.0
/*
 * 032_procfs_rw.c - A read/write /proc entry used as a config knob.
 *
 * echo 75 > /proc/training_threshold
 * cat /proc/training_threshold
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>

static int threshold = 50;

static ssize_t th_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	char tmp[16];
	int n = scnprintf(tmp, sizeof(tmp), "%d\n", threshold);

	return simple_read_from_buffer(buf, len, off, tmp, n);
}

static ssize_t th_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	int val, ret;

	ret = kstrtoint_from_user(buf, len, 10, &val);
	if (ret)
		return ret;
	if (val < 0 || val > 100)
		return -EINVAL;
	threshold = val;
	pr_info("procrw: threshold = %d\n", threshold);
	return len;
}

static const struct proc_ops th_ops = {
	.proc_read  = th_read,
	.proc_write = th_write,
};

static int __init th_init(void)
{
	return proc_create("training_threshold", 0666, NULL, &th_ops) ? 0 : -ENOMEM;
}

static void __exit th_exit(void)
{
	remove_proc_entry("training_threshold", NULL);
}

module_init(th_init);
module_exit(th_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read/write procfs entry");
