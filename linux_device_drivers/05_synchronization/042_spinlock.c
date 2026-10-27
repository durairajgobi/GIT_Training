// SPDX-License-Identifier: GPL-2.0
/*
 * 042_spinlock.c - Spinlocks shared between process and interrupt context.
 *
 * A timer callback (softirq context) and the read() path both touch a
 * statistics structure, so the process side uses spin_lock_bh().
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

static DEFINE_SPINLOCK(stats_lock);
static struct {
	unsigned long ticks;
	unsigned long last_jiffies;
} stats;
static struct timer_list tmr;

static void tmr_fn(struct timer_list *t)
{
	spin_lock(&stats_lock);		/* already in softirq context */
	stats.ticks++;
	stats.last_jiffies = jiffies;
	spin_unlock(&stats_lock);
	mod_timer(&tmr, jiffies + msecs_to_jiffies(100));
}

static ssize_t sl_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	unsigned long ticks, last;
	char tmp[64];
	int n;

	spin_lock_bh(&stats_lock);	/* block the timer softirq on this CPU */
	ticks = stats.ticks;
	last = stats.last_jiffies;
	spin_unlock_bh(&stats_lock);

	n = scnprintf(tmp, sizeof(tmp), "ticks=%lu last=%lu\n", ticks, last);
	return simple_read_from_buffer(buf, len, off, tmp, n);
}

static const struct file_operations sl_fops = {
	.owner = THIS_MODULE,
	.read  = sl_read,
};

static struct miscdevice sl_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "spinstats",
	.fops  = &sl_fops,
	.mode  = 0444,
};

static int __init sl_init(void)
{
	int ret = misc_register(&sl_misc);

	if (ret)
		return ret;
	timer_setup(&tmr, tmr_fn, 0);
	mod_timer(&tmr, jiffies + msecs_to_jiffies(100));
	return 0;
}

static void __exit sl_exit(void)
{
	timer_shutdown_sync(&tmr);
	misc_deregister(&sl_misc);
}

module_init(sl_init);
module_exit(sl_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Spinlock example");
