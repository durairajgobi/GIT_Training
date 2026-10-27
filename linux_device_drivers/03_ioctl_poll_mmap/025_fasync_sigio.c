// SPDX-License-Identifier: GPL-2.0
/*
 * 025_fasync_sigio.c - Asynchronous notification with SIGIO.
 *
 * A user process does:
 *   fcntl(fd, F_SETOWN, getpid());
 *   fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | FASYNC);
 * and receives SIGIO every time something writes to /dev/asyncdev.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/signal.h>

static struct fasync_struct *async_queue;

static int as_fasync(int fd, struct file *f, int on)
{
	return fasync_helper(fd, f, on, &async_queue);
}

static ssize_t as_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	pr_info("async: %zu bytes written, signalling readers\n", len);
	kill_fasync(&async_queue, SIGIO, POLL_IN);
	return len;
}

static int as_release(struct inode *inode, struct file *f)
{
	/* remove this file from the async notification list */
	return as_fasync(-1, f, 0);
}

static const struct file_operations as_fops = {
	.owner   = THIS_MODULE,
	.write   = as_write,
	.fasync  = as_fasync,
	.release = as_release,
};

static struct miscdevice as_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "asyncdev",
	.fops  = &as_fops,
	.mode  = 0666,
};

module_misc_device(as_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("fasync / SIGIO example");
