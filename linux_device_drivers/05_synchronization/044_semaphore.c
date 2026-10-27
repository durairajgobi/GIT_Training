// SPDX-License-Identifier: GPL-2.0
/*
 * 044_semaphore.c - A counting semaphore limiting concurrent users.
 *
 * At most "slots" processes may have /dev/sem_slots open at once; extra
 * openers sleep until a slot is released (or get -EBUSY with O_NONBLOCK).
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

static int slots = 2;
module_param(slots, int, 0444);

static struct semaphore sem;

static int ss_open(struct inode *inode, struct file *f)
{
	if (f->f_flags & O_NONBLOCK) {
		if (down_trylock(&sem))
			return -EBUSY;
	} else if (down_interruptible(&sem)) {
		return -ERESTARTSYS;
	}
	pr_info("sem: pid %d got a slot\n", current->pid);
	return 0;
}

static int ss_release(struct inode *inode, struct file *f)
{
	up(&sem);
	pr_info("sem: slot released\n");
	return 0;
}

static const struct file_operations ss_fops = {
	.owner   = THIS_MODULE,
	.open    = ss_open,
	.release = ss_release,
};

static struct miscdevice ss_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "sem_slots",
	.fops  = &ss_fops,
	.mode  = 0666,
};

static int __init ss_init(void)
{
	if (slots < 1)
		return -EINVAL;
	sema_init(&sem, slots);
	return misc_register(&ss_misc);
}

static void __exit ss_exit(void)
{
	misc_deregister(&ss_misc);
}

module_init(ss_init);
module_exit(ss_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Counting semaphore example");
