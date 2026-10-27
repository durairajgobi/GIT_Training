// SPDX-License-Identifier: GPL-2.0
/*
 * 054_workqueue.c - Deferring work to process context on the system workqueue.
 *
 * Writing to /dev/workq queues a work item that may sleep.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/atomic.h>

struct job {
	struct work_struct work;
	atomic_t pending;
};
static struct job job;

static void job_fn(struct work_struct *w)
{
	struct job *j = container_of(w, struct job, work);

	pr_info("workq: running in %s, pending=%d\n",
		current->comm, atomic_read(&j->pending));
	msleep(50);		/* allowed: process context */
	atomic_set(&j->pending, 0);
}

static ssize_t wq_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	atomic_inc(&job.pending);
	if (!schedule_work(&job.work))
		pr_info("workq: already queued, coalesced\n");
	return len;
}

static const struct file_operations wq_fops = {
	.owner = THIS_MODULE,
	.write = wq_write,
};

static struct miscdevice wq_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "workq",
	.fops  = &wq_fops,
	.mode  = 0222,
};

static int __init wq_init(void)
{
	INIT_WORK(&job.work, job_fn);
	return misc_register(&wq_misc);
}

static void __exit wq_exit(void)
{
	misc_deregister(&wq_misc);
	cancel_work_sync(&job.work);
}

module_init(wq_init);
module_exit(wq_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("System workqueue example");
