// SPDX-License-Identifier: GPL-2.0
/*
 * 046_completion.c - Waiting for an event with struct completion.
 *
 * A reader of /dev/completion blocks until someone writes to it.
 *   terminal 1: cat /dev/completion
 *   terminal 2: echo go > /dev/completion
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

static DECLARE_COMPLETION(event_done);

static ssize_t cp_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	pr_info("completion: %s waiting\n", current->comm);
	if (wait_for_completion_interruptible(&event_done))
		return -ERESTARTSYS;
	pr_info("completion: %s woken\n", current->comm);
	return 0;
}

static ssize_t cp_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	complete(&event_done);	/* wake exactly one waiter */
	return len;
}

static const struct file_operations cp_fops = {
	.owner = THIS_MODULE,
	.read  = cp_read,
	.write = cp_write,
};

static struct miscdevice cp_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "completion",
	.fops  = &cp_fops,
	.mode  = 0666,
};

static int __init cp_init(void)
{
	return misc_register(&cp_misc);
}

static void __exit cp_exit(void)
{
	complete_all(&event_done);	/* release any sleepers */
	misc_deregister(&cp_misc);
}

module_init(cp_init);
module_exit(cp_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Completion example");
