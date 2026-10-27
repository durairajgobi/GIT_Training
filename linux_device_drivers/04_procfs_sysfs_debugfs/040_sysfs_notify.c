// SPDX-License-Identifier: GPL-2.0
/*
 * 040_sysfs_notify.c - Waking user-space pollers on a sysfs attribute.
 *
 * A timer bumps "event" every second and calls sysfs_notify(), so a
 * program that poll()s /sys/kernel/training_notify/event (POLLPRI|POLLERR)
 * wakes up without busy-looping.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

static struct kobject *kobj;
static unsigned long event;
static struct timer_list tick;
static struct work_struct notify_work;

static ssize_t event_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%lu\n", event);
}
static struct kobj_attribute event_attr = __ATTR_RO(event);

static void notify_fn(struct work_struct *w)
{
	sysfs_notify(kobj, NULL, "event");	/* may sleep - run from process ctx */
}

static void tick_fn(struct timer_list *t)
{
	event++;
	schedule_work(&notify_work);
	mod_timer(&tick, jiffies + HZ);
}

static int __init sn_init(void)
{
	int ret;

	kobj = kobject_create_and_add("training_notify", kernel_kobj);
	if (!kobj)
		return -ENOMEM;
	ret = sysfs_create_file(kobj, &event_attr.attr);
	if (ret) {
		kobject_put(kobj);
		return ret;
	}
	INIT_WORK(&notify_work, notify_fn);
	timer_setup(&tick, tick_fn, 0);
	mod_timer(&tick, jiffies + HZ);
	return 0;
}

static void __exit sn_exit(void)
{
	timer_shutdown_sync(&tick);
	cancel_work_sync(&notify_work);
	kobject_put(kobj);
}

module_init(sn_init);
module_exit(sn_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("sysfs_notify example");
