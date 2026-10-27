// SPDX-License-Identifier: GPL-2.0
/*
 * 018_private_data.c - Per-open state stored in file->private_data.
 *
 * Every open() gets its own session counter; reads report how many
 * times *this* file descriptor has been read.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/atomic.h>

struct session {
	int id;
	unsigned int reads;
};

static atomic_t next_id = ATOMIC_INIT(0);

static int pd_open(struct inode *inode, struct file *f)
{
	struct session *s = kzalloc(sizeof(*s), GFP_KERNEL);

	if (!s)
		return -ENOMEM;
	s->id = atomic_inc_return(&next_id);
	f->private_data = s;
	pr_info("pdata: session %d opened\n", s->id);
	return 0;
}

static int pd_release(struct inode *inode, struct file *f)
{
	struct session *s = f->private_data;

	pr_info("pdata: session %d closed after %u reads\n", s->id, s->reads);
	kfree(s);
	return 0;
}

static ssize_t pd_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	struct session *s = f->private_data;
	char tmp[64];
	int n;

	if (*off)
		return 0;
	s->reads++;
	n = scnprintf(tmp, sizeof(tmp), "session %d read #%u\n", s->id, s->reads);
	return simple_read_from_buffer(buf, len, off, tmp, n);
}

static const struct file_operations pd_fops = {
	.owner   = THIS_MODULE,
	.open    = pd_open,
	.release = pd_release,
	.read    = pd_read,
};

static struct miscdevice pd_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "pdata",
	.fops  = &pd_fops,
	.mode  = 0444,
};

module_misc_device(pd_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("file->private_data per-open state example");
