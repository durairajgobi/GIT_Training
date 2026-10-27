// SPDX-License-Identifier: GPL-2.0
/*
 * 015_read_write_buffer.c - A character device backed by a kernel buffer.
 *
 * echo "hello kernel" > /dev/rwbuf
 * cat /dev/rwbuf
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define BUF_SIZE 1024

static char data[BUF_SIZE];
static size_t data_len;
static DEFINE_MUTEX(data_lock);

static ssize_t rw_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	ssize_t ret = 0;

	mutex_lock(&data_lock);
	if (*off >= data_len)
		goto out;
	len = min(len, (size_t)(data_len - *off));
	if (copy_to_user(buf, data + *off, len)) {
		ret = -EFAULT;
		goto out;
	}
	*off += len;
	ret = len;
out:
	mutex_unlock(&data_lock);
	return ret;
}

static ssize_t rw_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	if (len > BUF_SIZE)
		len = BUF_SIZE;

	mutex_lock(&data_lock);
	if (copy_from_user(data, buf, len)) {
		mutex_unlock(&data_lock);
		return -EFAULT;
	}
	data_len = len;
	mutex_unlock(&data_lock);
	return len;
}

static const struct file_operations rw_fops = {
	.owner = THIS_MODULE,
	.read  = rw_read,
	.write = rw_write,
};

static struct miscdevice rw_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "rwbuf",
	.fops  = &rw_fops,
	.mode  = 0666,
};

module_misc_device(rw_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read/write kernel buffer char device");
