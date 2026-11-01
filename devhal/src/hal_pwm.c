/* SPDX-License-Identifier: MIT */
/*
 * hal_pwm.c - PWM channels via /sys/class/pwm.
 */
#include "devhal/hal_pwm.h"
#include "hal_internal.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MOD "pwm"

/* How long to wait for udev to expose a freshly exported channel. */
#define EXPORT_WAIT_MS  1000
#define EXPORT_POLL_US  10000

struct hal_pwm {
	unsigned int chip;
	unsigned int channel;
	bool exported_by_us;
	char dir[HAL_PATH_MAX];
	pthread_mutex_t lock;
};

static int attr_path(const hal_pwm_t *p, const char *attr, char *out,
		     size_t len)
{
	int n = snprintf(out, len, "%s/%s", p->dir, attr);

	return n < 0 || (size_t)n >= len ? -ENAMETOOLONG : 0;
}

static int attr_read_u64(const hal_pwm_t *p, const char *attr, uint64_t *v)
{
	char path[HAL_PATH_MAX];
	int r = attr_path(p, attr, path, sizeof(path));

	return r < 0 ? r : hal_sysfs_read_u64(path, v);
}

static int attr_write_u64(const hal_pwm_t *p, const char *attr, uint64_t v)
{
	char path[HAL_PATH_MAX];
	int r = attr_path(p, attr, path, sizeof(path));

	return r < 0 ? r : hal_sysfs_write_u64(path, v);
}

static int attr_write(const hal_pwm_t *p, const char *attr, const char *v)
{
	char path[HAL_PATH_MAX];
	int r = attr_path(p, attr, path, sizeof(path));

	return r < 0 ? r : hal_sysfs_write(path, v);
}

static int chip_write(const hal_pwm_t *p, const char *attr)
{
	char path[HAL_PATH_MAX], val[16];
	int r = hal_sysfs_path(path, sizeof(path),
			       "/sys/class/pwm/pwmchip%u/%s", p->chip, attr);

	if (r < 0)
		return r;
	snprintf(val, sizeof(val), "%u", p->channel);
	return hal_sysfs_write(path, val);
}

int hal_pwm_open(hal_pwm_t **out, unsigned int chip, unsigned int channel)
{
	char enable[HAL_PATH_MAX];
	hal_pwm_t *p;
	int64_t deadline;
	int r;

	if (!out)
		return -EINVAL;
	*out = NULL;

	p = calloc(1, sizeof(*p));
	if (!p)
		return -ENOMEM;
	p->chip = chip;
	p->channel = channel;
	r = hal_sysfs_path(p->dir, sizeof(p->dir),
			   "/sys/class/pwm/pwmchip%u/pwm%u", chip, channel);
	if (r == 0)
		r = attr_path(p, "enable", enable, sizeof(enable));
	if (r < 0)
		goto fail;

	if (hal_sys_access(p->dir, F_OK) < 0) {
		r = chip_write(p, "export");
		if (r < 0 && r != -EBUSY) {
			HAL_ERR(MOD, "export pwmchip%u/pwm%u: %s", chip,
				channel, hal_strerror(r));
			goto fail;
		}
		p->exported_by_us = r == 0;
	}

	/* The attributes appear (and get their udev permissions) later. */
	deadline = hal_now_ms() + EXPORT_WAIT_MS;
	while ((r = hal_sys_access(enable, W_OK)) < 0) {
		if (hal_remaining_ms(deadline) == 0) {
			HAL_ERR(MOD, "%s not writable: %s", enable,
				hal_strerror(r));
			if (p->exported_by_us)
				chip_write(p, "unexport");
			goto fail;
		}
		hal_sleep_us(EXPORT_POLL_US);
	}

	pthread_mutex_init(&p->lock, NULL);
	*out = p;
	return 0;
fail:
	free(p);
	return r;
}

int hal_pwm_close(hal_pwm_t *p)
{
	int r = 0;

	if (!p)
		return -EINVAL;
	if (p->exported_by_us)
		r = chip_write(p, "unexport");
	pthread_mutex_destroy(&p->lock);
	free(p);
	return r;
}

int hal_pwm_detach(hal_pwm_t *p)
{
	if (!p)
		return -EINVAL;
	p->exported_by_us = false;
	return hal_pwm_close(p);
}

static int get_state_locked(hal_pwm_t *p, hal_pwm_state_t *st)
{
	char path[HAL_PATH_MAX], pol[16];
	uint64_t en;
	int r;

	if ((r = attr_read_u64(p, "period", &st->period_ns)) < 0 ||
	    (r = attr_read_u64(p, "duty_cycle", &st->duty_ns)) < 0 ||
	    (r = attr_read_u64(p, "enable", &en)) < 0)
		return r;
	st->enabled = en != 0;

	r = attr_path(p, "polarity", path, sizeof(path));
	if (r == 0)
		r = hal_sysfs_read(path, pol, sizeof(pol));
	if (r < 0) {
		/* Not every controller exposes polarity. */
		st->polarity = HAL_PWM_POLARITY_NORMAL;
		return 0;
	}
	st->polarity = strcmp(pol, "inversed") == 0 ?
		       HAL_PWM_POLARITY_INVERSED : HAL_PWM_POLARITY_NORMAL;
	return 0;
}

int hal_pwm_get_state(hal_pwm_t *p, hal_pwm_state_t *st)
{
	int r;

	if (!p || !st)
		return -EINVAL;
	pthread_mutex_lock(&p->lock);
	r = get_state_locked(p, st);
	pthread_mutex_unlock(&p->lock);
	return r;
}

static int apply_locked(hal_pwm_t *p, const hal_pwm_state_t *want)
{
	hal_pwm_state_t cur;
	int r;

	if (want->period_ns == 0 || want->duty_ns > want->period_ns ||
	    want->polarity > HAL_PWM_POLARITY_INVERSED)
		return -EINVAL;
	r = get_state_locked(p, &cur);
	if (r < 0)
		return r;

	if (want->polarity != cur.polarity) {
		/* Most controllers refuse polarity changes while running. */
		if (cur.enabled) {
			r = attr_write(p, "enable", "0");
			if (r < 0)
				return r;
			cur.enabled = false;
		}
		r = attr_write(p, "polarity",
			       want->polarity == HAL_PWM_POLARITY_INVERSED ?
			       "inversed" : "normal");
		if (r < 0)
			return r;
	}

	/* Keep duty <= period at every intermediate step. */
	if (want->period_ns >= cur.duty_ns) {
		if (want->period_ns != cur.period_ns &&
		    (r = attr_write_u64(p, "period", want->period_ns)) < 0)
			return r;
		if (want->duty_ns != cur.duty_ns &&
		    (r = attr_write_u64(p, "duty_cycle", want->duty_ns)) < 0)
			return r;
	} else {
		if ((r = attr_write_u64(p, "duty_cycle", want->duty_ns)) < 0 ||
		    (r = attr_write_u64(p, "period", want->period_ns)) < 0)
			return r;
	}

	if (want->enabled != cur.enabled)
		r = attr_write(p, "enable", want->enabled ? "1" : "0");
	return r;
}

int hal_pwm_apply(hal_pwm_t *p, const hal_pwm_state_t *st)
{
	int r;

	if (!p || !st)
		return -EINVAL;
	pthread_mutex_lock(&p->lock);
	r = apply_locked(p, st);
	pthread_mutex_unlock(&p->lock);
	if (r < 0)
		HAL_ERR(MOD, "pwmchip%u/pwm%u apply: %s", p->chip, p->channel,
			hal_strerror(r));
	return r;
}

typedef void (*pwm_edit_fn)(hal_pwm_state_t *st, uint64_t arg);

/* Read-modify-write the state atomically with respect to this handle. */
static int pwm_edit(hal_pwm_t *p, pwm_edit_fn fn, uint64_t arg)
{
	hal_pwm_state_t st;
	int r;

	if (!p)
		return -EINVAL;
	pthread_mutex_lock(&p->lock);
	r = get_state_locked(p, &st);
	if (r == 0) {
		fn(&st, arg);
		r = apply_locked(p, &st);
	}
	pthread_mutex_unlock(&p->lock);
	return r;
}

static void edit_period(hal_pwm_state_t *st, uint64_t v)
{
	st->period_ns = v;
	if (st->duty_ns > v)
		st->duty_ns = v;
}

static void edit_duty(hal_pwm_state_t *st, uint64_t v)
{
	st->duty_ns = v;
}

static void edit_permille(hal_pwm_state_t *st, uint64_t v)
{
	st->duty_ns = (st->period_ns / 1000) * v +
		      (st->period_ns % 1000) * v / 1000;
}

static void edit_freq(hal_pwm_state_t *st, uint64_t hz)
{
	uint64_t period = (1000000000ull + hz / 2) / hz;
	double ratio = st->period_ns ?
		       (double)st->duty_ns / (double)st->period_ns : 0.0;

	st->period_ns = period;
	st->duty_ns = (uint64_t)(ratio * (double)period + 0.5);
	if (st->duty_ns > period)
		st->duty_ns = period;
}

static void edit_enable(hal_pwm_state_t *st, uint64_t on)
{
	st->enabled = on != 0;
}

int hal_pwm_set_period_ns(hal_pwm_t *p, uint64_t period_ns)
{
	if (period_ns == 0)
		return -EINVAL;
	return pwm_edit(p, edit_period, period_ns);
}

int hal_pwm_set_duty_ns(hal_pwm_t *p, uint64_t duty_ns)
{
	return pwm_edit(p, edit_duty, duty_ns);
}

int hal_pwm_set_duty_permille(hal_pwm_t *p, unsigned int permille)
{
	if (permille > 1000)
		return -EINVAL;
	return pwm_edit(p, edit_permille, permille);
}

int hal_pwm_set_frequency_hz(hal_pwm_t *p, uint32_t hz)
{
	if (hz == 0 || hz > 1000000000u)
		return -EINVAL;
	return pwm_edit(p, edit_freq, hz);
}

int hal_pwm_enable(hal_pwm_t *p)
{
	return pwm_edit(p, edit_enable, 1);
}

int hal_pwm_disable(hal_pwm_t *p)
{
	return pwm_edit(p, edit_enable, 0);
}
