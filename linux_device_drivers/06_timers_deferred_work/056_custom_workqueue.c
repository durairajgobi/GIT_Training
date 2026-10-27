// SPDX-License-Identifier: GPL-2.0
/*
 * 056_custom_workqueue.c - A dedicated ordered workqueue with many work items.
 *
 * An ordered workqueue executes one item at a time in queueing order,
 * useful when a driver's operations must be serialized.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/delay.h>

static int njobs = 5;
module_param(njobs, int, 0444);

struct numbered_work {
	struct work_struct work;
	int id;
};

static struct workqueue_struct *wq;

static void numbered_fn(struct work_struct *w)
{
	struct numbered_work *nw = container_of(w, struct numbered_work, work);

	pr_info("cwq: job %d start\n", nw->id);
	msleep(100);
	pr_info("cwq: job %d done\n", nw->id);
	kfree(nw);
}

static int __init cwq_init(void)
{
	int i;

	wq = alloc_ordered_workqueue("training_wq", 0);
	if (!wq)
		return -ENOMEM;

	for (i = 0; i < njobs; i++) {
		struct numbered_work *nw = kmalloc(sizeof(*nw), GFP_KERNEL);

		if (!nw)
			break;
		nw->id = i;
		INIT_WORK(&nw->work, numbered_fn);
		queue_work(wq, &nw->work);
	}
	return 0;
}

static void __exit cwq_exit(void)
{
	destroy_workqueue(wq);	/* drains outstanding work first */
}

module_init(cwq_init);
module_exit(cwq_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Custom ordered workqueue example");
