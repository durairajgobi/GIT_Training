/* SPDX-License-Identifier: MIT */
/*
 * hal_backlight.c - Client of the pwm_lcd_bl kernel driver.
 */
#include "devhal/hal_backlight.h"
#include "hal_internal.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "pwm_lcd_bl.h"   /* ../pwm_lcd_backlight/include/uapi */

#define MOD "backlight"
#define FADE_MAX_STEPS 100

struct hal_backlight {
	int fd;
	pthread_mutex_t lock;
};

int hal_backlight_open(hal_backlight_t **out, const char *path)
{
	hal_backlight_t *b;
	int fd;

	if (!out)
		return -EINVAL;
	*out = NULL;
	if (!path)
		path = "/dev/" LCD_BL_DEV_NAME;

	fd = hal_sys_open(path, O_RDWR);
	if (fd < 0) {
		HAL_ERR(MOD, "open %s: %s", path, hal_strerror(fd));
		return fd;
	}
	b = calloc(1, sizeof(*b));
	if (!b) {
		hal_sys_close(fd);
		return -ENOMEM;
	}
	b->fd = fd;
	pthread_mutex_init(&b->lock, NULL);
	*out = b;
	return 0;
}

int hal_backlight_close(hal_backlight_t *b)
{
	int r;

	if (!b)
		return -EINVAL;
	r = hal_sys_close(b->fd);
	pthread_mutex_destroy(&b->lock);
	free(b);
	return r;
}

int hal_backlight_enable(hal_backlight_t *b)
{
	return b ? hal_sys_ioctl(b->fd, LCD_BL_IOC_ENABLE, NULL) : -EINVAL;
}

int hal_backlight_disable(hal_backlight_t *b)
{
	return b ? hal_sys_ioctl(b->fd, LCD_BL_IOC_DISABLE, NULL) : -EINVAL;
}

static int get_cfg(hal_backlight_t *b, struct lcd_bl_config *c)
{
	memset(c, 0, sizeof(*c));
	return hal_sys_ioctl(b->fd, LCD_BL_IOC_GET_CONFIG, c);
}

int hal_backlight_get_state(hal_backlight_t *b, hal_backlight_state_t *st)
{
	struct lcd_bl_config c;
	int r;

	if (!b || !st)
		return -EINVAL;
	r = get_cfg(b, &c);
	if (r < 0)
		return r;
	st->period_ns = c.period_ns;
	st->duty_ns = c.duty_ns;
	st->inversed = c.polarity == LCD_BL_POLARITY_INVERSED;
	st->enabled = c.enabled != 0;
	st->brightness = c.brightness;
	st->max_brightness = c.max_brightness;
	return 0;
}

static int set_level(hal_backlight_t *b, uint32_t level)
{
	__u32 v = level;

	return hal_sys_ioctl(b->fd, LCD_BL_IOC_SET_BRIGHTNESS, &v);
}

int hal_backlight_set_brightness(hal_backlight_t *b, uint32_t level)
{
	int r;

	if (!b)
		return -EINVAL;
	pthread_mutex_lock(&b->lock);
	r = set_level(b, level);
	pthread_mutex_unlock(&b->lock);
	return r;
}

int hal_backlight_get_brightness(hal_backlight_t *b, uint32_t *level)
{
	struct lcd_bl_config c;
	int r;

	if (!b || !level)
		return -EINVAL;
	r = get_cfg(b, &c);
	if (r == 0)
		*level = c.brightness;
	return r;
}

int hal_backlight_set_percent(hal_backlight_t *b, unsigned int percent)
{
	struct lcd_bl_config c;
	int r;

	if (!b || percent > 100)
		return -EINVAL;
	pthread_mutex_lock(&b->lock);
	r = get_cfg(b, &c);
	if (r == 0)
		r = set_level(b, (uint32_t)(((uint64_t)c.max_brightness *
					     percent + 50) / 100));
	pthread_mutex_unlock(&b->lock);
	return r;
}

int hal_backlight_set_pwm(hal_backlight_t *b, uint64_t period_ns,
			  uint64_t duty_ns, bool inversed)
{
	struct lcd_bl_config c;
	int r;

	if (!b || period_ns == 0 || duty_ns > period_ns)
		return -EINVAL;
	memset(&c, 0, sizeof(c));
	c.period_ns = period_ns;
	c.duty_ns = duty_ns;
	c.polarity = inversed ? LCD_BL_POLARITY_INVERSED
			      : LCD_BL_POLARITY_NORMAL;
	pthread_mutex_lock(&b->lock);
	r = hal_sys_ioctl(b->fd, LCD_BL_IOC_SET_CONFIG, &c);
	pthread_mutex_unlock(&b->lock);
	return r;
}

int hal_backlight_fade(hal_backlight_t *b, uint32_t target,
		       unsigned int duration_ms, unsigned int steps)
{
	struct lcd_bl_config c;
	uint32_t start, span;
	unsigned int i;
	int r;

	if (!b)
		return -EINVAL;

	/* The lock is held for the whole ramp so two fades never interleave. */
	pthread_mutex_lock(&b->lock);
	r = get_cfg(b, &c);
	if (r < 0)
		goto out;
	if (target > c.max_brightness) {
		r = -EINVAL;
		goto out;
	}
	start = c.brightness;
	span = start > target ? start - target : target - start;
	if (span == 0)
		goto out;
	if (steps == 0 || steps > span)
		steps = span;
	if (steps > FADE_MAX_STEPS)
		steps = FADE_MAX_STEPS;

	for (i = 1; i <= steps; i++) {
		uint32_t delta = (uint32_t)((uint64_t)span * i / steps);
		uint32_t level = start > target ? start - delta : start + delta;

		r = set_level(b, level);
		if (r < 0)
			break;
		if (i < steps && duration_ms)
			hal_sleep_us((unsigned int)((uint64_t)duration_ms *
						    1000u / steps));
	}
out:
	pthread_mutex_unlock(&b->lock);
	return r;
}
