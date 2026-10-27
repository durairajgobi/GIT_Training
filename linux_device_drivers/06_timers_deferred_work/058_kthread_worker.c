// SPDX-License-Identifier: GPL-2.0
/*
 * 058_kthread_worker.c - kthread_worker: a private thread that runs queued work.
 *
 * Gives the predictability of a dedicated thread (priority, CPU) with the
 * convenience of the work-item API.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>

static struct kthread_worker *worker;
static struct kthread_work work;
static struct kthread_delayed_work dwork;

static void work_fn(struct kthread_work *w)
{
	pr_info("kworker: immediate work in %s\n", current->comm);
}

static void dwork_fn(struct kthread_work *w)
{
	pr_info("kworker: delayed work in %s\n", current->comm);
	kthread_queue_delayed_work(worker, &dwork, 2 * HZ);
}

static int __init kw_init(void)
{
	worker = kthread_create_worker(0, "training_kworker");
	if (IS_ERR(worker))
		return PTR_ERR(worker);

	sched_set_fifo(worker->task);	/* real-time priority for latency-critical work */

	kthread_init_work(&work, work_fn);
	kthread_init_delayed_work(&dwork, dwork_fn);
	kthread_queue_work(worker, &work);
	kthread_queue_delayed_work(worker, &dwork, HZ);
	return 0;
}

static void __exit kw_exit(void)
{
	kthread_cancel_delayed_work_sync(&dwork);
	kthread_destroy_worker(worker);
}

module_init(kw_init);
module_exit(kw_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kthread_worker example");
