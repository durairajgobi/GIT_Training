// SPDX-License-Identifier: GPL-2.0
/*
 * 057_kthread.c - A kernel thread bound to a CPU that stops cleanly.
 *
 * ps -ef | grep training_kthread
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>

static int cpu;
module_param(cpu, int, 0444);

static struct task_struct *task;

static int thread_fn(void *data)
{
	unsigned long loops = 0;

	pr_info("kthread: started on cpu %d\n", smp_processor_id());
	while (!kthread_should_stop()) {
		loops++;
		if (loops % 5 == 0)
			pr_info("kthread: %lu loops\n", loops);
		msleep_interruptible(1000);
	}
	pr_info("kthread: stopping after %lu loops\n", loops);
	return 0;
}

static int __init kt_init(void)
{
	if (cpu < 0 || cpu >= nr_cpu_ids || !cpu_online(cpu))
		return -EINVAL;

	task = kthread_create(thread_fn, NULL, "training_kthread/%d", cpu);
	if (IS_ERR(task))
		return PTR_ERR(task);
	kthread_bind(task, cpu);
	wake_up_process(task);
	return 0;
}

static void __exit kt_exit(void)
{
	kthread_stop(task);
}

module_init(kt_init);
module_exit(kt_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel thread example");
