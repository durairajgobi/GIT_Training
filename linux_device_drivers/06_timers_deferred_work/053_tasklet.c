// SPDX-License-Identifier: GPL-2.0
/*
 * 053_tasklet.c - Tasklets as a softirq-context bottom half.
 *
 * Note: tasklets are considered legacy; new code should prefer threaded
 * IRQs or workqueues (see 054/062), but many drivers still use them.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/timer.h>

struct tl_ctx {
	struct tasklet_struct tasklet;
	struct timer_list timer;
	unsigned int runs;
};
static struct tl_ctx ctx;

static void tasklet_fn(struct tasklet_struct *t)
{
	struct tl_ctx *c = from_tasklet(c, t, tasklet);

	c->runs++;
	pr_info("tasklet: run %u on cpu %d\n", c->runs, smp_processor_id());
}

/* the timer plays the role of a "top half" that schedules the tasklet */
static void timer_fn(struct timer_list *t)
{
	tasklet_schedule(&ctx.tasklet);
	mod_timer(&ctx.timer, jiffies + HZ);
}

static int __init tl_init(void)
{
	tasklet_setup(&ctx.tasklet, tasklet_fn);
	timer_setup(&ctx.timer, timer_fn, 0);
	mod_timer(&ctx.timer, jiffies + HZ);
	return 0;
}

static void __exit tl_exit(void)
{
	timer_shutdown_sync(&ctx.timer);
	tasklet_kill(&ctx.tasklet);
	pr_info("tasklet: ran %u times\n", ctx.runs);
}

module_init(tl_init);
module_exit(tl_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Tasklet example");
