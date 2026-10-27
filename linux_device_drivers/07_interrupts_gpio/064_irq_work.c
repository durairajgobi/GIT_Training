// SPDX-License-Identifier: GPL-2.0
/*
 * 064_irq_work.c - Raising a callback in hard-IRQ context with irq_work.
 *
 * irq_work lets code running in NMI or other restricted contexts defer
 * a small job to a regular IRQ context as soon as possible.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/irq_work.h>
#include <linux/hardirq.h>

static struct irq_work iw;

static void iw_fn(struct irq_work *w)
{
	pr_info("irq_work: running in_hardirq=%d cpu=%d\n",
		!!in_hardirq(), smp_processor_id());
}

static int __init iw_init(void)
{
	init_irq_work(&iw, iw_fn);
	irq_work_queue(&iw);
	return 0;
}

static void __exit iw_exit(void)
{
	irq_work_sync(&iw);
}

module_init(iw_init);
module_exit(iw_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("irq_work example");
