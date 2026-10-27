// SPDX-License-Identifier: GPL-2.0
/*
 * 030_blocking_pipe.c - A ring-buffer "pipe" with blocking reads and writes.
 *
 * Shows wait_event_interruptible() on both sides of a bounded buffer,
 * the classic producer/consumer driver pattern.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define RING 64

static char ring[RING];
static unsigned int head, tail;	/* head = write pos, tail = read pos */
static DEFINE_MUTEX(bp_lock);
static DECLARE_WAIT_QUEUE_HEAD(inq);
static DECLARE_WAIT_QUEUE_HEAD(outq);

static unsigned int used(void)  { return head - tail; }
static unsigned int space(void) { return RING - used(); }

static ssize_t bp_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	size_t i, n;
	char tmp[RING];

	if (mutex_lock_interruptible(&bp_lock))
		return -ERESTARTSYS;
	while (!used()) {
		mutex_unlock(&bp_lock);
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(inq, used()))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&bp_lock))
			return -ERESTARTSYS;
	}
	n = min_t(size_t, len, used());
	for (i = 0; i < n; i++)
		tmp[i] = ring[tail++ % RING];
	mutex_unlock(&bp_lock);

	wake_up_interruptible(&outq);
	return copy_to_user(buf, tmp, n) ? -EFAULT : n;
}

static ssize_t bp_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	size_t i, n;
	char tmp[RING];

	if (mutex_lock_interruptible(&bp_lock))
		return -ERESTARTSYS;
	while (!space()) {
		mutex_unlock(&bp_lock);
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(outq, space()))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&bp_lock))
			return -ERESTARTSYS;
	}
	n = min_t(size_t, len, space());
	if (copy_from_user(tmp, buf, n)) {
		mutex_unlock(&bp_lock);
		return -EFAULT;
	}
	for (i = 0; i < n; i++)
		ring[head++ % RING] = tmp[i];
	mutex_unlock(&bp_lock);

	wake_up_interruptible(&inq);
	return n;
}

static const struct file_operations bp_fops = {
	.owner = THIS_MODULE,
	.read  = bp_read,
	.write = bp_write,
};

static struct miscdevice bp_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "bpipe",
	.fops  = &bp_fops,
	.mode  = 0666,
};

module_misc_device(bp_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Blocking ring buffer pipe");
