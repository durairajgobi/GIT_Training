// SPDX-License-Identifier: GPL-2.0
/*
 * 047_wait_queue.c - Wait queues with a condition and a timeout.
 *
 * A kernel thread sleeps on a wait queue until "flag" is set via a module
 * parameter, or until 5 seconds pass.
 *   echo 1 > /sys/module/047_wait_queue/parameters/flag
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>

static DECLARE_WAIT_QUEUE_HEAD(wq);
static int flag;
static struct task_struct *waiter;

static int flag_set(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);

	if (!ret)
		wake_up_interruptible(&wq);
	return ret;
}

static const struct kernel_param_ops flag_ops = {
	.set = flag_set,
	.get = param_get_int,
};
module_param_cb(flag, &flag_ops, &flag, 0644);

static int waiter_fn(void *data)
{
	while (!kthread_should_stop()) {
		long left = wait_event_interruptible_timeout(wq,
					READ_ONCE(flag) || kthread_should_stop(),
					5 * HZ);
		if (kthread_should_stop())
			break;
		if (left == 0) {
			pr_info("waitq: timed out, still waiting\n");
		} else if (left > 0) {
			pr_info("waitq: woken with flag=%d\n", flag);
			WRITE_ONCE(flag, 0);
		}
	}
	return 0;
}

static int __init wq_init(void)
{
	waiter = kthread_run(waiter_fn, NULL, "waitq_demo");
	return PTR_ERR_OR_ZERO(waiter);
}

static void __exit wq_exit(void)
{
	kthread_stop(waiter);
}

module_init(wq_init);
module_exit(wq_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Wait queue example");
