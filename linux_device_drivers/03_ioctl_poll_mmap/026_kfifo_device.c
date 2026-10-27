// SPDX-License-Identifier: GPL-2.0
/*
 * 026_kfifo_device.c - A FIFO character device using the kfifo API.
 *
 * Writes append to the fifo; reads drain it. Blocking and non-blocking
 * modes are both supported.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#define FIFO_SIZE 1024	/* must be a power of two */

static DEFINE_KFIFO(fifo, char, FIFO_SIZE);
static DEFINE_MUTEX(read_lock);
static DEFINE_MUTEX(write_lock);
static DECLARE_WAIT_QUEUE_HEAD(rq);
static DECLARE_WAIT_QUEUE_HEAD(wq);

static ssize_t kf_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	unsigned int copied;
	int ret;

	if (kfifo_is_empty(&fifo)) {
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(rq, !kfifo_is_empty(&fifo)))
			return -ERESTARTSYS;
	}
	if (mutex_lock_interruptible(&read_lock))
		return -ERESTARTSYS;
	ret = kfifo_to_user(&fifo, buf, len, &copied);
	mutex_unlock(&read_lock);
	wake_up_interruptible(&wq);
	return ret ? ret : copied;
}

static ssize_t kf_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	unsigned int copied;
	int ret;

	if (kfifo_is_full(&fifo)) {
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(wq, !kfifo_is_full(&fifo)))
			return -ERESTARTSYS;
	}
	if (mutex_lock_interruptible(&write_lock))
		return -ERESTARTSYS;
	ret = kfifo_from_user(&fifo, buf, len, &copied);
	mutex_unlock(&write_lock);
	wake_up_interruptible(&rq);
	return ret ? ret : copied;
}

static const struct file_operations kf_fops = {
	.owner = THIS_MODULE,
	.read  = kf_read,
	.write = kf_write,
};

static struct miscdevice kf_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "kfifo_dev",
	.fops  = &kf_fops,
	.mode  = 0666,
};

module_misc_device(kf_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kfifo based character device");
