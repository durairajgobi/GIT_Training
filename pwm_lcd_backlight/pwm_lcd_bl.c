// SPDX-License-Identifier: GPL-2.0
/*
 * pwm_lcd_bl.c - PWM driven LCD backlight with a user-space control interface
 *
 * The driver binds to a device-tree node (compatible "training,pwm-lcd-backlight")
 * that references a PWM channel and, optionally, a backlight enable GPIO.
 *
 * User space can control the backlight through:
 *   - /dev/lcd_backlight  : ioctls defined in include/uapi/pwm_lcd_bl.h
 *   - /sys/class/misc/lcd_backlight/{enable,period_ns,duty_ns,polarity,
 *                                    brightness,max_brightness}
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/kref.h>
#include <linux/math64.h>
#include <linux/miscdevice.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "include/uapi/pwm_lcd_bl.h"

#define DRV_NAME			"pwm-lcd-backlight"
#define DEFAULT_MAX_BRIGHTNESS		255

static unsigned int default_period_ns = 50000;	/* 20 kHz */
module_param(default_period_ns, uint, 0444);
MODULE_PARM_DESC(default_period_ns,
		 "PWM period used when the device tree does not provide one (default 50000 ns)");

struct pwm_lcd_bl {
	struct device *dev;
	struct pwm_device *pwm;		/* NULL once the device has been removed */
	struct gpio_desc *enable_gpio;
	struct miscdevice miscdev;
	struct mutex lock;		/* protects everything below and @pwm */
	struct kref ref;		/* keeps the struct alive for open files */

	bool enabled;
	bool suspended_enabled;
	u64 period_ns;
	u64 duty_ns;
	enum pwm_polarity polarity;
	u32 max_brightness;
};

static int bl_pwm_apply(struct pwm_device *pwm, const struct pwm_state *state)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	return pwm_apply_might_sleep(pwm, state);
#else
	return pwm_apply_state(pwm, state);
#endif
}

static void pwm_lcd_bl_release(struct kref *ref)
{
	struct pwm_lcd_bl *bl = container_of(ref, struct pwm_lcd_bl, ref);

	mutex_destroy(&bl->lock);
	kfree(bl);
}

/*
 * Push the requested configuration to the hardware. On failure the
 * previous configuration is kept so the software state always mirrors
 * what the PWM controller is actually doing. Caller holds bl->lock.
 */
static int pwm_lcd_bl_update(struct pwm_lcd_bl *bl, bool enable, u64 period_ns,
			     u64 duty_ns, enum pwm_polarity polarity)
{
	struct pwm_state state;
	int ret;

	if (!bl->pwm)
		return -ENODEV;
	if (!period_ns || duty_ns > period_ns)
		return -EINVAL;

	/* Switch the panel enable line off before the PWM goes away */
	if (!enable && bl->enable_gpio)
		gpiod_set_value_cansleep(bl->enable_gpio, 0);

	pwm_init_state(bl->pwm, &state);
	state.period = period_ns;
	state.duty_cycle = duty_ns;
	state.polarity = polarity;
	state.enabled = enable;

	ret = bl_pwm_apply(bl->pwm, &state);
	if (ret) {
		dev_err(bl->dev, "failed to apply PWM state (period=%llu duty=%llu): %d\n",
			period_ns, duty_ns, ret);
		if (!enable && bl->enabled && bl->enable_gpio)
			gpiod_set_value_cansleep(bl->enable_gpio, 1);
		return ret;
	}

	if (enable && bl->enable_gpio)
		gpiod_set_value_cansleep(bl->enable_gpio, 1);

	bl->enabled = enable;
	bl->period_ns = period_ns;
	bl->duty_ns = duty_ns;
	bl->polarity = polarity;
	return 0;
}

static int pwm_lcd_bl_set_enable(struct pwm_lcd_bl *bl, bool enable)
{
	int ret;

	mutex_lock(&bl->lock);
	ret = pwm_lcd_bl_update(bl, enable, bl->period_ns, bl->duty_ns, bl->polarity);
	mutex_unlock(&bl->lock);
	return ret;
}

static u32 pwm_lcd_bl_get_brightness(struct pwm_lcd_bl *bl)
{
	if (!bl->period_ns)
		return 0;
	return (u32)mul_u64_u64_div_u64(bl->duty_ns, bl->max_brightness, bl->period_ns);
}

static int pwm_lcd_bl_set_brightness(struct pwm_lcd_bl *bl, u32 brightness)
{
	u64 duty;
	int ret;

	mutex_lock(&bl->lock);
	if (brightness > bl->max_brightness) {
		ret = -EINVAL;
		goto out;
	}
	duty = mul_u64_u64_div_u64(bl->period_ns, brightness, bl->max_brightness);
	ret = pwm_lcd_bl_update(bl, bl->enabled, bl->period_ns, duty, bl->polarity);
out:
	mutex_unlock(&bl->lock);
	return ret;
}

/* ------------------------------------------------------------------------ */
/* Character device interface                                                */
/* ------------------------------------------------------------------------ */

static struct pwm_lcd_bl *to_bl(struct miscdevice *misc)
{
	return container_of(misc, struct pwm_lcd_bl, miscdev);
}

static int pwm_lcd_bl_open(struct inode *inode, struct file *file)
{
	/* misc_open() stored our miscdevice in private_data */
	struct pwm_lcd_bl *bl = to_bl(file->private_data);

	kref_get(&bl->ref);
	file->private_data = bl;
	return 0;
}

static int pwm_lcd_bl_close(struct inode *inode, struct file *file)
{
	struct pwm_lcd_bl *bl = file->private_data;

	kref_put(&bl->ref, pwm_lcd_bl_release);
	return 0;
}

static long pwm_lcd_bl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pwm_lcd_bl *bl = file->private_data;
	void __user *uarg = (void __user *)arg;
	struct lcd_bl_config cfg;
	u32 brightness;
	int ret;

	switch (cmd) {
	case LCD_BL_IOC_ENABLE:
		return pwm_lcd_bl_set_enable(bl, true);

	case LCD_BL_IOC_DISABLE:
		return pwm_lcd_bl_set_enable(bl, false);

	case LCD_BL_IOC_SET_CONFIG:
		if (copy_from_user(&cfg, uarg, sizeof(cfg)))
			return -EFAULT;
		if (cfg.polarity != LCD_BL_POLARITY_NORMAL &&
		    cfg.polarity != LCD_BL_POLARITY_INVERSED)
			return -EINVAL;
		mutex_lock(&bl->lock);
		ret = pwm_lcd_bl_update(bl, bl->enabled, cfg.period_ns, cfg.duty_ns,
					cfg.polarity == LCD_BL_POLARITY_INVERSED ?
					PWM_POLARITY_INVERSED : PWM_POLARITY_NORMAL);
		mutex_unlock(&bl->lock);
		return ret;

	case LCD_BL_IOC_GET_CONFIG:
		memset(&cfg, 0, sizeof(cfg));
		mutex_lock(&bl->lock);
		if (!bl->pwm) {
			mutex_unlock(&bl->lock);
			return -ENODEV;
		}
		cfg.period_ns = bl->period_ns;
		cfg.duty_ns = bl->duty_ns;
		cfg.polarity = bl->polarity == PWM_POLARITY_INVERSED ?
			       LCD_BL_POLARITY_INVERSED : LCD_BL_POLARITY_NORMAL;
		cfg.enabled = bl->enabled;
		cfg.brightness = pwm_lcd_bl_get_brightness(bl);
		cfg.max_brightness = bl->max_brightness;
		mutex_unlock(&bl->lock);
		return copy_to_user(uarg, &cfg, sizeof(cfg)) ? -EFAULT : 0;

	case LCD_BL_IOC_SET_BRIGHTNESS:
		if (get_user(brightness, (__u32 __user *)uarg))
			return -EFAULT;
		return pwm_lcd_bl_set_brightness(bl, brightness);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations pwm_lcd_bl_fops = {
	.owner		= THIS_MODULE,
	.open		= pwm_lcd_bl_open,
	.release	= pwm_lcd_bl_close,
	.unlocked_ioctl	= pwm_lcd_bl_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= noop_llseek,
};

/* ------------------------------------------------------------------------ */
/* sysfs interface (attached to the misc device)                            */
/* ------------------------------------------------------------------------ */

static struct pwm_lcd_bl *dev_to_bl(struct device *dev)
{
	return to_bl(dev_get_drvdata(dev));
}

static ssize_t enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pwm_lcd_bl *bl = dev_to_bl(dev);

	return sysfs_emit(buf, "%d\n", bl->enabled);
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;
	ret = pwm_lcd_bl_set_enable(dev_to_bl(dev), enable);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(enable);

static ssize_t period_ns_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%llu\n", dev_to_bl(dev)->period_ns);
}

static ssize_t period_ns_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct pwm_lcd_bl *bl = dev_to_bl(dev);
	u64 period, duty;
	int ret;

	ret = kstrtou64(buf, 0, &period);
	if (ret)
		return ret;
	if (!period)
		return -EINVAL;

	mutex_lock(&bl->lock);
	/* Keep the same relative brightness when the period changes */
	duty = bl->period_ns ? mul_u64_u64_div_u64(bl->duty_ns, period, bl->period_ns) : 0;
	ret = pwm_lcd_bl_update(bl, bl->enabled, period, duty, bl->polarity);
	mutex_unlock(&bl->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(period_ns);

static ssize_t duty_ns_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%llu\n", dev_to_bl(dev)->duty_ns);
}

static ssize_t duty_ns_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct pwm_lcd_bl *bl = dev_to_bl(dev);
	u64 duty;
	int ret;

	ret = kstrtou64(buf, 0, &duty);
	if (ret)
		return ret;

	mutex_lock(&bl->lock);
	ret = pwm_lcd_bl_update(bl, bl->enabled, bl->period_ns, duty, bl->polarity);
	mutex_unlock(&bl->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(duty_ns);

static ssize_t polarity_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", dev_to_bl(dev)->polarity == PWM_POLARITY_INVERSED ?
			  "inversed" : "normal");
}

static ssize_t polarity_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct pwm_lcd_bl *bl = dev_to_bl(dev);
	enum pwm_polarity polarity;
	int ret;

	if (sysfs_streq(buf, "normal"))
		polarity = PWM_POLARITY_NORMAL;
	else if (sysfs_streq(buf, "inversed"))
		polarity = PWM_POLARITY_INVERSED;
	else
		return -EINVAL;

	mutex_lock(&bl->lock);
	ret = pwm_lcd_bl_update(bl, bl->enabled, bl->period_ns, bl->duty_ns, polarity);
	mutex_unlock(&bl->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(polarity);

static ssize_t brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pwm_lcd_bl *bl = dev_to_bl(dev);
	u32 brightness;

	mutex_lock(&bl->lock);
	brightness = pwm_lcd_bl_get_brightness(bl);
	mutex_unlock(&bl->lock);
	return sysfs_emit(buf, "%u\n", brightness);
}

static ssize_t brightness_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	u32 brightness;
	int ret;

	ret = kstrtou32(buf, 0, &brightness);
	if (ret)
		return ret;
	ret = pwm_lcd_bl_set_brightness(dev_to_bl(dev), brightness);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(brightness);

static ssize_t max_brightness_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	return sysfs_emit(buf, "%u\n", dev_to_bl(dev)->max_brightness);
}
static DEVICE_ATTR_RO(max_brightness);

static struct attribute *pwm_lcd_bl_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_period_ns.attr,
	&dev_attr_duty_ns.attr,
	&dev_attr_polarity.attr,
	&dev_attr_brightness.attr,
	&dev_attr_max_brightness.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pwm_lcd_bl);

/* ------------------------------------------------------------------------ */
/* Platform driver                                                           */
/* ------------------------------------------------------------------------ */

static int pwm_lcd_bl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_lcd_bl *bl;
	struct pwm_state state;
	u32 brightness;
	int ret;

	bl = kzalloc(sizeof(*bl), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;
	bl->dev = dev;
	mutex_init(&bl->lock);
	kref_init(&bl->ref);

	bl->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(bl->pwm)) {
		ret = dev_err_probe(dev, PTR_ERR(bl->pwm), "unable to request PWM\n");
		goto err_put;
	}

	bl->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(bl->enable_gpio)) {
		ret = dev_err_probe(dev, PTR_ERR(bl->enable_gpio),
				    "unable to request enable GPIO\n");
		goto err_put;
	}

	if (device_property_read_u32(dev, "max-brightness", &bl->max_brightness) ||
	    !bl->max_brightness)
		bl->max_brightness = DEFAULT_MAX_BRIGHTNESS;
	if (device_property_read_u32(dev, "default-brightness", &brightness))
		brightness = bl->max_brightness;
	brightness = min(brightness, bl->max_brightness);

	/* Period and polarity come from the "pwms" cells when available */
	pwm_init_state(bl->pwm, &state);
	bl->period_ns = state.period ? state.period : default_period_ns;
	bl->polarity = state.polarity;
	bl->duty_ns = mul_u64_u64_div_u64(bl->period_ns, brightness, bl->max_brightness);

	/* Start with the backlight off; user space decides when to enable */
	mutex_lock(&bl->lock);
	ret = pwm_lcd_bl_update(bl, false, bl->period_ns, bl->duty_ns, bl->polarity);
	mutex_unlock(&bl->lock);
	if (ret)
		goto err_put;

	bl->miscdev.minor = MISC_DYNAMIC_MINOR;
	bl->miscdev.name = LCD_BL_DEV_NAME;
	bl->miscdev.fops = &pwm_lcd_bl_fops;
	bl->miscdev.parent = dev;
	bl->miscdev.groups = pwm_lcd_bl_groups;
	bl->miscdev.mode = 0660;

	ret = misc_register(&bl->miscdev);
	if (ret) {
		dev_err(dev, "failed to register misc device: %d\n", ret);
		goto err_put;
	}

	platform_set_drvdata(pdev, bl);
	dev_info(dev, "registered /dev/%s (period %llu ns, max brightness %u)\n",
		 LCD_BL_DEV_NAME, bl->period_ns, bl->max_brightness);
	return 0;

err_put:
	kref_put(&bl->ref, pwm_lcd_bl_release);
	return ret;
}

static void pwm_lcd_bl_teardown(struct platform_device *pdev)
{
	struct pwm_lcd_bl *bl = platform_get_drvdata(pdev);

	/* No new opens or sysfs accesses after this returns */
	misc_deregister(&bl->miscdev);

	mutex_lock(&bl->lock);
	pwm_lcd_bl_update(bl, false, bl->period_ns, bl->duty_ns, bl->polarity);
	/* The PWM is released by devres; open files must not touch it anymore */
	bl->pwm = NULL;
	mutex_unlock(&bl->lock);

	kref_put(&bl->ref, pwm_lcd_bl_release);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void pwm_lcd_bl_remove(struct platform_device *pdev)
{
	pwm_lcd_bl_teardown(pdev);
}
#else
static int pwm_lcd_bl_remove(struct platform_device *pdev)
{
	pwm_lcd_bl_teardown(pdev);
	return 0;
}
#endif

static void pwm_lcd_bl_shutdown(struct platform_device *pdev)
{
	struct pwm_lcd_bl *bl = platform_get_drvdata(pdev);

	pwm_lcd_bl_set_enable(bl, false);
}

static int pwm_lcd_bl_suspend(struct device *dev)
{
	struct pwm_lcd_bl *bl = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&bl->lock);
	bl->suspended_enabled = bl->enabled;
	ret = pwm_lcd_bl_update(bl, false, bl->period_ns, bl->duty_ns, bl->polarity);
	mutex_unlock(&bl->lock);
	return ret;
}

static int pwm_lcd_bl_resume(struct device *dev)
{
	struct pwm_lcd_bl *bl = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&bl->lock);
	ret = pwm_lcd_bl_update(bl, bl->suspended_enabled, bl->period_ns, bl->duty_ns,
				bl->polarity);
	mutex_unlock(&bl->lock);
	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pwm_lcd_bl_pm_ops, pwm_lcd_bl_suspend, pwm_lcd_bl_resume);

static const struct of_device_id pwm_lcd_bl_of_match[] = {
	{ .compatible = "training,pwm-lcd-backlight" },
	{ }
};
MODULE_DEVICE_TABLE(of, pwm_lcd_bl_of_match);

static struct platform_driver pwm_lcd_bl_driver = {
	.probe		= pwm_lcd_bl_probe,
	.remove		= pwm_lcd_bl_remove,
	.shutdown	= pwm_lcd_bl_shutdown,
	.driver		= {
		.name		= DRV_NAME,
		.of_match_table	= pwm_lcd_bl_of_match,
		.pm		= pm_sleep_ptr(&pwm_lcd_bl_pm_ops),
	},
};
module_platform_driver(pwm_lcd_bl_driver);

MODULE_AUTHOR("Durairaj Gobi");
MODULE_DESCRIPTION("PWM LCD backlight driver with user-space enable/disable/config interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
