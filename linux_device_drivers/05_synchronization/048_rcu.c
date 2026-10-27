// SPDX-License-Identifier: GPL-2.0
/*
 * 048_rcu.c - Read-Copy-Update protecting a configuration pointer.
 *
 * Readers dereference the config locklessly; the updater publishes a new
 * copy with rcu_assign_pointer() and frees the old one after a grace period.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/delay.h>

struct config {
	int version;
	int timeout_ms;
	struct rcu_head rcu;
};

static struct config __rcu *cur_cfg;
static DEFINE_SPINLOCK(update_lock);
static struct task_struct *reader, *updater;

static int reader_fn(void *data)
{
	while (!kthread_should_stop()) {
		struct config *c;

		rcu_read_lock();
		c = rcu_dereference(cur_cfg);
		if (c)
			pr_info_ratelimited("rcu: reader sees v%d timeout=%d\n",
					    c->version, c->timeout_ms);
		rcu_read_unlock();
		msleep(100);
	}
	return 0;
}

static int update_cfg(int version)
{
	struct config *new, *old;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;
	new->version = version;
	new->timeout_ms = version * 10;

	spin_lock(&update_lock);
	old = rcu_dereference_protected(cur_cfg, lockdep_is_held(&update_lock));
	rcu_assign_pointer(cur_cfg, new);
	spin_unlock(&update_lock);

	if (old)
		kfree_rcu(old, rcu);
	return 0;
}

static int updater_fn(void *data)
{
	int v = 1;

	while (!kthread_should_stop()) {
		update_cfg(++v);
		msleep(1000);
	}
	return 0;
}

static int __init rcu_demo_init(void)
{
	int ret = update_cfg(1);

	if (ret)
		return ret;
	reader = kthread_run(reader_fn, NULL, "rcu_reader");
	updater = kthread_run(updater_fn, NULL, "rcu_updater");
	return 0;
}

static void __exit rcu_demo_exit(void)
{
	struct config *c;

	if (!IS_ERR(reader))
		kthread_stop(reader);
	if (!IS_ERR(updater))
		kthread_stop(updater);

	c = rcu_dereference_protected(cur_cfg, 1);
	RCU_INIT_POINTER(cur_cfg, NULL);
	synchronize_rcu();
	kfree(c);
	rcu_barrier();		/* wait for pending kfree_rcu callbacks */
}

module_init(rcu_demo_init);
module_exit(rcu_demo_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RCU example");
