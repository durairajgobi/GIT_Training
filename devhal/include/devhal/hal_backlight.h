/* SPDX-License-Identifier: MIT */
/*
 * hal_backlight.h - LCD backlight control on top of the pwm_lcd_bl driver
 *                   (../pwm_lcd_backlight, /dev/lcd_backlight).
 */
#ifndef DEVHAL_HAL_BACKLIGHT_H
#define DEVHAL_HAL_BACKLIGHT_H

#include "hal_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_backlight hal_backlight_t;

typedef struct {
	uint64_t period_ns;
	uint64_t duty_ns;
	bool     inversed;
	bool     enabled;
	uint32_t brightness;
	uint32_t max_brightness;
} hal_backlight_state_t;

/* @path NULL opens the default /dev/lcd_backlight. */
int hal_backlight_open(hal_backlight_t **out, const char *path);
int hal_backlight_close(hal_backlight_t *bl);

int hal_backlight_enable(hal_backlight_t *bl);
int hal_backlight_disable(hal_backlight_t *bl);
int hal_backlight_get_state(hal_backlight_t *bl, hal_backlight_state_t *st);

int hal_backlight_set_brightness(hal_backlight_t *bl, uint32_t level);
int hal_backlight_get_brightness(hal_backlight_t *bl, uint32_t *level);
/* 0..100 % of max_brightness, rounded to nearest level. */
int hal_backlight_set_percent(hal_backlight_t *bl, unsigned int percent);
int hal_backlight_set_pwm(hal_backlight_t *bl, uint64_t period_ns,
			  uint64_t duty_ns, bool inversed);

/*
 * Ramp linearly from the current level to @target over @duration_ms using
 * @steps intermediate writes (0 = one step per level, capped at 100).
 * Blocks the caller for roughly @duration_ms.
 */
int hal_backlight_fade(hal_backlight_t *bl, uint32_t target,
		       unsigned int duration_ms, unsigned int steps);

#ifdef __cplusplus
}
#endif

#endif /* DEVHAL_HAL_BACKLIGHT_H */
