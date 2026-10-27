// SPDX-License-Identifier: GPL-2.0
/*
 * 031_procfs_read.c - A read-only /proc entry.
 *
 * cat /proc/training_info
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/jiffies.h>
#include <linux/fs.h>

static ssize_t pr_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	char tmp[128];
	int n = scnprintf(tmp, sizeof(tmp), "uptime_jiffies=%lu HZ=%d\n",
			  jiffies, HZ);

	return simple_read_from_buffer(buf, len, off, tmp, n);
}

static const struct proc_ops pr_ops = {
	.proc_read = pr_read,
};

static int __init pr_init(void)
{
	if (!proc_create("training_info", 0444, NULL, &pr_ops))
		return -ENOMEM;
	return 0;
}

static void __exit pr_exit(void)
{
	remove_proc_entry("training_info", NULL);
}

module_init(pr_init);
module_exit(pr_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only procfs entry");
