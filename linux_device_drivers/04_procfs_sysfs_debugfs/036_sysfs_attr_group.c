// SPDX-License-Identifier: GPL-2.0
/*
 * 036_sysfs_attr_group.c - Several attributes registered as one group.
 *
 * /sys/kernel/training_group/{speed,mode,stats}
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>

static struct kobject *kobj;
static int speed = 100;
static char mode[16] = "auto";
static unsigned long stores;

static ssize_t speed_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", speed);
}

static ssize_t speed_store(struct kobject *k, struct kobj_attribute *a,
			   const char *buf, size_t count)
{
	int v, ret = kstrtoint(buf, 0, &v);

	if (ret)
		return ret;
	if (v < 10 || v > 1000)
		return -ERANGE;
	speed = v;
	stores++;
	return count;
}

static ssize_t mode_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%s\n", mode);
}

static ssize_t mode_store(struct kobject *k, struct kobj_attribute *a,
			  const char *buf, size_t count)
{
	if (sysfs_streq(buf, "auto") || sysfs_streq(buf, "manual")) {
		strscpy(mode, buf, sizeof(mode));
		strim(mode);
		stores++;
		return count;
	}
	return -EINVAL;
}

static ssize_t stats_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "stores=%lu\n", stores);
}

static struct kobj_attribute speed_attr = __ATTR_RW(speed);
static struct kobj_attribute mode_attr  = __ATTR_RW(mode);
static struct kobj_attribute stats_attr = __ATTR_RO(stats);

static struct attribute *attrs[] = {
	&speed_attr.attr,
	&mode_attr.attr,
	&stats_attr.attr,
	NULL,
};

static const struct attribute_group group = {
	.attrs = attrs,
};

static int __init ag_init(void)
{
	int ret;

	kobj = kobject_create_and_add("training_group", kernel_kobj);
	if (!kobj)
		return -ENOMEM;
	ret = sysfs_create_group(kobj, &group);
	if (ret)
		kobject_put(kobj);
	return ret;
}

static void __exit ag_exit(void)
{
	sysfs_remove_group(kobj, &group);
	kobject_put(kobj);
}

module_init(ag_init);
module_exit(ag_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("sysfs attribute group example");
