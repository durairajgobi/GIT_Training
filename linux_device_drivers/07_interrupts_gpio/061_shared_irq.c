// SPDX-License-Identifier: GPL-2.0
/*
 * 061_shared_irq.c - Attaching a handler to an existing shared IRQ line.
 *
 * Classic LDD exercise: hook the keyboard/network IRQ and count events
 * without disturbing the real driver.
 *   grep -i -e i8042 -e eth /proc/interrupts      # find a shared line
 *   sudo insmod 061_shared_irq.ko irq=1
 *   grep shared_irq_demo /proc/interrupts
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>

static int irq = 1;
module_param(irq, int, 0444);
MODULE_PARM_DESC(irq, "IRQ line to share");

static atomic_t count = ATOMIC_INIT(0);
static int dev_cookie;	/* unique dev_id required for shared IRQs */

static irqreturn_t shared_handler(int irq, void *dev_id)
{
	atomic_inc(&count);
	/* We did not cause this interrupt, let the real owner handle it. */
	return IRQ_NONE;
}

static int __init si_init(void)
{
	int ret = request_irq(irq, shared_handler, IRQF_SHARED,
			      "shared_irq_demo", &dev_cookie);
	if (ret)
		pr_err("shared_irq: cannot share irq %d (%d)\n", irq, ret);
	return ret;
}

static void __exit si_exit(void)
{
	free_irq(irq, &dev_cookie);
	pr_info("shared_irq: saw %d interrupts on line %d\n",
		atomic_read(&count), irq);
}

module_init(si_init);
module_exit(si_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Shared IRQ handler example");
