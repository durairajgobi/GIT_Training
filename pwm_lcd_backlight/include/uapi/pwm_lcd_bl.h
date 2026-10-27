/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * pwm_lcd_bl.h - User-space API for the PWM LCD backlight driver
 *
 * Shared between the kernel module and user-space applications.
 * The driver exposes a character device (/dev/lcd_backlight) that
 * accepts the ioctls below, plus sysfs attributes under
 * /sys/class/misc/lcd_backlight/.
 */
#ifndef _UAPI_PWM_LCD_BL_H
#define _UAPI_PWM_LCD_BL_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <linux/types.h>
#include <sys/ioctl.h>
#endif

#define LCD_BL_DEV_NAME		"lcd_backlight"

/* Values for lcd_bl_config.polarity */
#define LCD_BL_POLARITY_NORMAL		0
#define LCD_BL_POLARITY_INVERSED	1

/**
 * struct lcd_bl_config - PWM backlight configuration
 * @period_ns:      PWM period in nanoseconds (must be > 0)
 * @duty_ns:        PWM active time in nanoseconds (must be <= period_ns)
 * @polarity:       LCD_BL_POLARITY_NORMAL or LCD_BL_POLARITY_INVERSED
 * @enabled:        (read-only) 1 if the backlight is currently on
 * @brightness:     (read-only) duty cycle scaled to 0..max_brightness
 * @max_brightness: (read-only) brightness scale maximum
 *
 * LCD_BL_IOC_SET_CONFIG only uses @period_ns, @duty_ns and @polarity;
 * the remaining fields are filled in by LCD_BL_IOC_GET_CONFIG.
 */
struct lcd_bl_config {
	__u64 period_ns;
	__u64 duty_ns;
	__u32 polarity;
	__u32 enabled;
	__u32 brightness;
	__u32 max_brightness;
};

#define LCD_BL_IOC_MAGIC	'B'

/* Turn the backlight on using the current configuration */
#define LCD_BL_IOC_ENABLE		_IO(LCD_BL_IOC_MAGIC, 1)
/* Turn the backlight off (configuration is retained) */
#define LCD_BL_IOC_DISABLE		_IO(LCD_BL_IOC_MAGIC, 2)
/* Set period / duty / polarity; applied immediately if enabled */
#define LCD_BL_IOC_SET_CONFIG		_IOW(LCD_BL_IOC_MAGIC, 3, struct lcd_bl_config)
/* Read back the full configuration and state */
#define LCD_BL_IOC_GET_CONFIG		_IOR(LCD_BL_IOC_MAGIC, 4, struct lcd_bl_config)
/* Set brightness (0..max_brightness); recomputes duty from period */
#define LCD_BL_IOC_SET_BRIGHTNESS	_IOW(LCD_BL_IOC_MAGIC, 5, __u32)

#endif /* _UAPI_PWM_LCD_BL_H */
