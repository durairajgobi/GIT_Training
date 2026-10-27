// SPDX-License-Identifier: GPL-2.0
/*
 * 063_irq_top_bottom.c - Top half queues data, bottom half (workqueue) consumes it.
 *
 * The top half reads a "data register" and pushes it into a lock-free
 * kfifo; the work item drains the fifo in process context.
 * Uses the shared-IRQ trick from 061 so it can run on any PC.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <linux/jiffies.h>

static int irq = 1;
module_param(irq, int, 0444);

static DEFINE_KFIFO(samples, unsigned long, 64);
static struct work_struct bh_work;
static int cookie;

static void bh_fn(struct work_struct *w)
{
	unsigned long v;

	while (kfifo_get(&samples, &v))
		pr_info("tb: bottom half processed sample %lu\n", v);
}

static irqreturn_t top_half(int irq, void *dev_id)
{
	/* single producer (this handler) + single consumer: no lock needed */
	kfifo_put(&samples, jiffies);
	schedule_work(&bh_work);
	return IRQ_NONE;	/* not our device, see 061 */
}

static int __init tb_init(void)
{
	INIT_WORK(&bh_work, bh_fn);
	return request_irq(irq, top_half, IRQF_SHARED, "top_bottom_demo", &cookie);
}

static void __exit tb_exit(void)
{
	free_irq(irq, &cookie);
	cancel_work_sync(&bh_work);
}

module_init(tb_init);
module_exit(tb_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Top half / bottom half split example");
