// SPDX-License-Identifier: GPL-2.0
/*
 * 098_kobject_uevent.c - Sending custom uevents to udev / user space.
 *
 * udevadm monitor --kernel --property
 * echo 1 > /sys/class/misc/uevent_demo/trigger
 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/fs.h>

static unsigned int seq;
static struct miscdevice ue_misc;

static ssize_t trigger_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	char ev[32], seqs[32];
	char *envp[] = { ev, seqs, NULL };

	snprintf(ev, sizeof(ev), "TRAINING_EVENT=button");
	snprintf(seqs, sizeof(seqs), "TRAINING_SEQ=%u", ++seq);
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	return count;
}
static DEVICE_ATTR_WO(trigger);

static struct attribute *ue_attrs[] = {
	&dev_attr_trigger.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ue);

static const struct file_operations ue_fops = {
	.owner = THIS_MODULE,
};

static struct miscdevice ue_misc = {
	.minor  = MISC_DYNAMIC_MINOR,
	.name   = "uevent_demo",
	.fops   = &ue_fops,
	.groups = ue_groups,
};

module_misc_device(ue_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kobject uevent example");
