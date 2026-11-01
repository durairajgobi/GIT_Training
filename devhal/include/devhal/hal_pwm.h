/* SPDX-License-Identifier: MIT */
/*
 * hal_pwm.h - Generic PWM channels via /sys/class/pwm.
 *
 * hal_pwm_open() exports the channel if needed and waits for udev to make
 * the attribute files available. hal_pwm_close() unexports it only if this
 * handle exported it, so a channel configured by someone else is left alone.
 */
#ifndef DEVHAL_HAL_PWM_H
#define DEVHAL_HAL_PWM_H

#include "hal_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_pwm hal_pwm_t;

typedef enum {
	HAL_PWM_POLARITY_NORMAL = 0,
	HAL_PWM_POLARITY_INVERSED,
} hal_pwm_polarity_t;

typedef struct {
	uint64_t           period_ns;
	uint64_t           duty_ns;
	hal_pwm_polarity_t polarity;
	bool               enabled;
} hal_pwm_state_t;

int hal_pwm_open(hal_pwm_t **out, unsigned int chip, unsigned int channel);
int hal_pwm_close(hal_pwm_t *pwm);
/*
 * Free the handle but leave the channel exported and running, e.g. for a
 * tool that sets a PWM output and exits.
 */
int hal_pwm_detach(hal_pwm_t *pwm);

/*
 * Apply a full state. Writes are ordered so the kernel never sees
 * duty > period, and polarity is only changed while disabled.
 */
int hal_pwm_apply(hal_pwm_t *pwm, const hal_pwm_state_t *state);
int hal_pwm_get_state(hal_pwm_t *pwm, hal_pwm_state_t *state);

int hal_pwm_set_period_ns(hal_pwm_t *pwm, uint64_t period_ns);
int hal_pwm_set_duty_ns(hal_pwm_t *pwm, uint64_t duty_ns);
/* Duty as parts-per-thousand of the current period (0..1000). */
int hal_pwm_set_duty_permille(hal_pwm_t *pwm, unsigned int permille);
int hal_pwm_set_frequency_hz(hal_pwm_t *pwm, uint32_t hz); /* keeps duty ratio */
int hal_pwm_enable(hal_pwm_t *pwm);
int hal_pwm_disable(hal_pwm_t *pwm);

#ifdef __cplusplus
}
#endif

#endif /* DEVHAL_HAL_PWM_H */
