// SPDX-License-Identifier: GPL-2.0
/*
 * 017_multi_minor.c - One driver, several device instances (minors).
 *
 * Creates /dev/multi0 .. /dev/multi3, each with its own buffer.
 * echo one > /dev/multi1 ; cat /dev/multi1 ; cat /dev/multi2
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/mutex.h>

#define NUM_DEVS 4
#define BUF_SIZE 256

struct multi_dev {
	struct cdev cdev;
	char buf[BUF_SIZE];
	size_t len;
	struct mutex lock;
};

static dev_t base;
static struct class *m_class;
static struct multi_dev devs[NUM_DEVS];

static int m_open(struct inode *inode, struct file *f)
{
	f->private_data = container_of(inode->i_cdev, struct multi_dev, cdev);
	return 0;
}

static ssize_t m_read(struct file *f, char __user *ubuf, size_t len, loff_t *off)
{
	struct multi_dev *d = f->private_data;
	ssize_t ret;

	mutex_lock(&d->lock);
	ret = simple_read_from_buffer(ubuf, len, off, d->buf, d->len);
	mutex_unlock(&d->lock);
	return ret;
}

static ssize_t m_write(struct file *f, const char __user *ubuf, size_t len, loff_t *off)
{
	struct multi_dev *d = f->private_data;
	ssize_t ret;

	mutex_lock(&d->lock);
	ret = simple_write_to_buffer(d->buf, BUF_SIZE, off, ubuf, len);
	if (ret > 0)
		d->len = max_t(size_t, d->len, *off);
	mutex_unlock(&d->lock);
	return ret;
}

static const struct file_operations m_fops = {
	.owner = THIS_MODULE,
	.open  = m_open,
	.read  = m_read,
	.write = m_write,
};

static int __init m_init(void)
{
	int i, ret;

	ret = alloc_chrdev_region(&base, 0, NUM_DEVS, "multi");
	if (ret)
		return ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	m_class = class_create("multi");
#else
	m_class = class_create(THIS_MODULE, "multi");
#endif
	if (IS_ERR(m_class)) {
		unregister_chrdev_region(base, NUM_DEVS);
		return PTR_ERR(m_class);
	}

	for (i = 0; i < NUM_DEVS; i++) {
		mutex_init(&devs[i].lock);
		cdev_init(&devs[i].cdev, &m_fops);
		ret = cdev_add(&devs[i].cdev, MKDEV(MAJOR(base), i), 1);
		if (ret)
			goto err;
		device_create(m_class, NULL, MKDEV(MAJOR(base), i), NULL, "multi%d", i);
	}
	return 0;

err:
	while (--i >= 0) {
		device_destroy(m_class, MKDEV(MAJOR(base), i));
		cdev_del(&devs[i].cdev);
	}
	class_destroy(m_class);
	unregister_chrdev_region(base, NUM_DEVS);
	return ret;
}

static void __exit m_exit(void)
{
	int i;

	for (i = 0; i < NUM_DEVS; i++) {
		device_destroy(m_class, MKDEV(MAJOR(base), i));
		cdev_del(&devs[i].cdev);
	}
	class_destroy(m_class);
	unregister_chrdev_region(base, NUM_DEVS);
}

module_init(m_init);
module_exit(m_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multiple minor devices example");
