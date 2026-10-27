// SPDX-License-Identifier: GPL-2.0
/*
 * 037_device_attr.c - DEVICE_ATTR attributes attached to a misc device.
 *
 * cat /sys/class/misc/attrdev/brightness
 * echo 128 > /sys/class/misc/attrdev/brightness
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/fs.h>

static unsigned int brightness = 50;

static ssize_t brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", brightness);
}

static ssize_t brightness_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned int v;
	int ret = kstrtouint(buf, 0, &v);

	if (ret)
		return ret;
	brightness = min(v, 255U);
	dev_info(dev, "brightness set to %u\n", brightness);
	return count;
}
static DEVICE_ATTR_RW(brightness);

static ssize_t max_brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "255\n");
}
static DEVICE_ATTR_RO(max_brightness);

static struct attribute *attrdev_attrs[] = {
	&dev_attr_brightness.attr,
	&dev_attr_max_brightness.attr,
	NULL,
};
ATTRIBUTE_GROUPS(attrdev);

static const struct file_operations attrdev_fops = {
	.owner = THIS_MODULE,
};

static struct miscdevice attrdev = {
	.minor  = MISC_DYNAMIC_MINOR,
	.name   = "attrdev",
	.fops   = &attrdev_fops,
	.groups = attrdev_groups,	/* created atomically with the device */
};

module_misc_device(attrdev);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Device attributes example");
