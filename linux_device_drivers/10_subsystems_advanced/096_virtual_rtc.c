// SPDX-License-Identifier: GPL-2.0
/*
 * 096_virtual_rtc.c - A virtual RTC driver backed by an offset from system time.
 *
 * sudo hwclock -r -f /dev/rtcN
 * sudo hwclock -w -f /dev/rtcN
 */
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/timekeeping.h>

struct vrtc {
	struct rtc_device *rtc;
	time64_t offset;	/* rtc time = system time + offset */
};

static struct platform_device *pdev_self;

static int vrtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct vrtc *v = dev_get_drvdata(dev);

	rtc_time64_to_tm(ktime_get_real_seconds() + v->offset, tm);
	return 0;
}

static int vrtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct vrtc *v = dev_get_drvdata(dev);

	v->offset = rtc_tm_to_time64(tm) - ktime_get_real_seconds();
	dev_info(dev, "time set, offset now %lld s\n", v->offset);
	return 0;
}

static const struct rtc_class_ops vrtc_ops = {
	.read_time = vrtc_read_time,
	.set_time  = vrtc_set_time,
};

static int vrtc_probe(struct platform_device *pdev)
{
	struct vrtc *v = devm_kzalloc(&pdev->dev, sizeof(*v), GFP_KERNEL);

	if (!v)
		return -ENOMEM;
	platform_set_drvdata(pdev, v);

	v->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(v->rtc))
		return PTR_ERR(v->rtc);
	v->rtc->ops = &vrtc_ops;
	v->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	v->rtc->range_max = RTC_TIMESTAMP_END_2099;
	return devm_rtc_register_device(v->rtc);
}

static struct platform_driver vrtc_driver = {
	.probe = vrtc_probe,
	.driver = { .name = "training-vrtc" },
};

static int __init vrtc_init(void)
{
	int ret = platform_driver_register(&vrtc_driver);

	if (ret)
		return ret;
	pdev_self = platform_device_register_simple("training-vrtc", -1, NULL, 0);
	if (IS_ERR(pdev_self)) {
		platform_driver_unregister(&vrtc_driver);
		return PTR_ERR(pdev_self);
	}
	return 0;
}

static void __exit vrtc_exit(void)
{
	platform_device_unregister(pdev_self);
	platform_driver_unregister(&vrtc_driver);
}

module_init(vrtc_init);
module_exit(vrtc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual RTC example");
