// SPDX-License-Identifier: GPL-2.0
/*
 * 029_scull_quantum.c - A simplified LDD3 "scull" device.
 *
 * Memory grows on demand in fixed-size quanta kept in a linked list,
 * so the device can hold arbitrarily large data (bounded by max_size).
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define QUANTUM 1024

static unsigned long max_size = 1 << 20;
module_param(max_size, ulong, 0444);

struct quantum {
	struct list_head node;
	char data[QUANTUM];
};

static LIST_HEAD(qlist);
static size_t dev_size;
static DEFINE_MUTEX(sc_lock);

static struct quantum *get_quantum(size_t idx, bool create)
{
	struct quantum *q;
	size_t i = 0;

	list_for_each_entry(q, &qlist, node)
		if (i++ == idx)
			return q;

	while (create && i++ <= idx) {
		q = kzalloc(sizeof(*q), GFP_KERNEL);
		if (!q)
			return NULL;
		list_add_tail(&q->node, &qlist);
	}
	return create ? q : NULL;
}

static void sc_trim(void)
{
	struct quantum *q, *tmp;

	list_for_each_entry_safe(q, tmp, &qlist, node) {
		list_del(&q->node);
		kfree(q);
	}
	dev_size = 0;
}

static int sc_open(struct inode *inode, struct file *f)
{
	if ((f->f_flags & O_ACCMODE) == O_WRONLY && (f->f_flags & O_TRUNC)) {
		mutex_lock(&sc_lock);
		sc_trim();
		mutex_unlock(&sc_lock);
	}
	return 0;
}

static ssize_t sc_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	struct quantum *q;
	size_t pos, chunk;
	ssize_t ret = 0;

	mutex_lock(&sc_lock);
	if (*off >= dev_size)
		goto out;
	len = min_t(size_t, len, dev_size - *off);
	pos = *off % QUANTUM;
	chunk = min_t(size_t, len, QUANTUM - pos);	/* one quantum per call */
	q = get_quantum(*off / QUANTUM, false);
	if (!q)
		goto out;
	if (copy_to_user(buf, q->data + pos, chunk)) {
		ret = -EFAULT;
		goto out;
	}
	*off += chunk;
	ret = chunk;
out:
	mutex_unlock(&sc_lock);
	return ret;
}

static ssize_t sc_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	struct quantum *q;
	size_t pos, chunk;
	ssize_t ret;

	if (*off >= max_size)
		return -ENOSPC;

	mutex_lock(&sc_lock);
	pos = *off % QUANTUM;
	chunk = min_t(size_t, len, QUANTUM - pos);
	q = get_quantum(*off / QUANTUM, true);
	if (!q) {
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(q->data + pos, buf, chunk)) {
		ret = -EFAULT;
		goto out;
	}
	*off += chunk;
	dev_size = max_t(size_t, dev_size, *off);
	ret = chunk;
out:
	mutex_unlock(&sc_lock);
	return ret;
}

static const struct file_operations sc_fops = {
	.owner = THIS_MODULE,
	.open  = sc_open,
	.read  = sc_read,
	.write = sc_write,
	.llseek = default_llseek,
};

static struct miscdevice sc_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "scull",
	.fops  = &sc_fops,
	.mode  = 0666,
};

static int __init sc_init(void)
{
	return misc_register(&sc_misc);
}

static void __exit sc_exit(void)
{
	misc_deregister(&sc_misc);
	sc_trim();
}

module_init(sc_init);
module_exit(sc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simplified scull device");
