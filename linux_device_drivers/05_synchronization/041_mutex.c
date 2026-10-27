// SPDX-License-Identifier: GPL-2.0
/*
 * 041_mutex.c - Protecting shared data with a mutex across kernel threads.
 *
 * Two threads increment a shared counter; with the mutex the final
 * value is always exactly 2 * iterations.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/completion.h>

static int iterations = 100000;
module_param(iterations, int, 0444);

static DEFINE_MUTEX(counter_lock);
static unsigned long counter;
static DECLARE_COMPLETION(done_a);
static DECLARE_COMPLETION(done_b);

static int worker(void *data)
{
	struct completion *done = data;
	int i;

	for (i = 0; i < iterations; i++) {
		mutex_lock(&counter_lock);
		counter++;
		mutex_unlock(&counter_lock);
	}
	complete(done);
	return 0;
}

static int __init mx_init(void)
{
	struct task_struct *a, *b;

	a = kthread_run(worker, &done_a, "mx_worker_a");
	if (IS_ERR(a))
		return PTR_ERR(a);
	b = kthread_run(worker, &done_b, "mx_worker_b");
	if (IS_ERR(b)) {
		wait_for_completion(&done_a);
		return PTR_ERR(b);
	}

	wait_for_completion(&done_a);
	wait_for_completion(&done_b);
	pr_info("mutex: counter=%lu (expected %d)\n", counter, 2 * iterations);
	return 0;
}

static void __exit mx_exit(void)
{
	pr_info("mutex: exit\n");
}

module_init(mx_init);
module_exit(mx_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mutex example");
