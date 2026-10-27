// SPDX-License-Identifier: GPL-2.0
/*
 * 049_seqlock.c - seqlock for small data that is written rarely and read often.
 *
 * Readers never block writers; they simply retry if a write happened
 * while they were reading.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/seqlock.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

static DEFINE_SEQLOCK(pos_lock);
static struct { long x, y, z; } pos;
static struct timer_list tmr;

static void tmr_fn(struct timer_list *t)
{
	write_seqlock(&pos_lock);
	pos.x++;
	pos.y += 2;
	pos.z += 3;
	write_sequnlock(&pos_lock);
	mod_timer(&tmr, jiffies + HZ / 10);
}

static ssize_t sq_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	long x, y, z;
	unsigned int seq;
	char tmp[96];
	int n;

	do {
		seq = read_seqbegin(&pos_lock);
		x = pos.x;
		y = pos.y;
		z = pos.z;
	} while (read_seqretry(&pos_lock, seq));

	n = scnprintf(tmp, sizeof(tmp), "x=%ld y=%ld z=%ld consistent=%d\n",
		      x, y, z, y == 2 * x && z == 3 * x);
	return simple_read_from_buffer(buf, len, off, tmp, n);
}

static const struct file_operations sq_fops = {
	.owner = THIS_MODULE,
	.read  = sq_read,
};

static struct miscdevice sq_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "seqpos",
	.fops  = &sq_fops,
	.mode  = 0444,
};

static int __init sq_init(void)
{
	int ret = misc_register(&sq_misc);

	if (ret)
		return ret;
	timer_setup(&tmr, tmr_fn, 0);
	mod_timer(&tmr, jiffies + HZ / 10);
	return 0;
}

static void __exit sq_exit(void)
{
	timer_shutdown_sync(&tmr);
	misc_deregister(&sq_misc);
}

module_init(sq_init);
module_exit(sq_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("seqlock example");
