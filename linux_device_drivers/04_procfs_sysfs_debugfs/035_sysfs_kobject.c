// SPDX-License-Identifier: GPL-2.0
/*
 * 035_sysfs_kobject.c - Creating a kobject with attributes under /sys/kernel.
 *
 * cat  /sys/kernel/training/value
 * echo 9 > /sys/kernel/training/value
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

static struct kobject *training_kobj;
static int value;

static ssize_t value_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t value_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	int ret = kstrtoint(buf, 10, &value);

	return ret ? ret : count;
}

static struct kobj_attribute value_attr = __ATTR_RW(value);

static int __init sk_init(void)
{
	int ret;

	training_kobj = kobject_create_and_add("training", kernel_kobj);
	if (!training_kobj)
		return -ENOMEM;

	ret = sysfs_create_file(training_kobj, &value_attr.attr);
	if (ret)
		kobject_put(training_kobj);
	return ret;
}

static void __exit sk_exit(void)
{
	kobject_put(training_kobj);	/* also removes its attributes */
}

module_init(sk_init);
module_exit(sk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("sysfs kobject example");
