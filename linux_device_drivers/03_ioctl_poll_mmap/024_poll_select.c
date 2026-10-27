// SPDX-License-Identifier: GPL-2.0
/*
 * 024_poll_select.c - Supporting poll()/select()/epoll().
 *
 * Writers push data; readers block (or poll) until data is available.
 *   terminal 1: cat /dev/polldev
 *   terminal 2: echo ping > /dev/polldev
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define BUF_SIZE 256

static char buf[BUF_SIZE];
static size_t buf_len;
static DEFINE_MUTEX(lock);
static DECLARE_WAIT_QUEUE_HEAD(read_wq);

static ssize_t pl_read(struct file *f, char __user *ubuf, size_t len, loff_t *off)
{
	ssize_t ret;

	mutex_lock(&lock);
	while (!buf_len) {
		mutex_unlock(&lock);
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(read_wq, READ_ONCE(buf_len) > 0))
			return -ERESTARTSYS;
		mutex_lock(&lock);
	}
	len = min(len, buf_len);
	ret = copy_to_user(ubuf, buf, len) ? -EFAULT : len;
	buf_len = 0;		/* consume */
	mutex_unlock(&lock);
	return ret;
}

static ssize_t pl_write(struct file *f, const char __user *ubuf, size_t len, loff_t *off)
{
	len = min_t(size_t, len, BUF_SIZE);
	mutex_lock(&lock);
	if (copy_from_user(buf, ubuf, len)) {
		mutex_unlock(&lock);
		return -EFAULT;
	}
	buf_len = len;
	mutex_unlock(&lock);
	wake_up_interruptible(&read_wq);
	return len;
}

static __poll_t pl_poll(struct file *f, poll_table *wait)
{
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;	/* always writable */

	poll_wait(f, &read_wq, wait);
	if (READ_ONCE(buf_len))
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static const struct file_operations pl_fops = {
	.owner = THIS_MODULE,
	.read  = pl_read,
	.write = pl_write,
	.poll  = pl_poll,
};

static struct miscdevice pl_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "polldev",
	.fops  = &pl_fops,
	.mode  = 0666,
};

module_misc_device(pl_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("poll/select example");
