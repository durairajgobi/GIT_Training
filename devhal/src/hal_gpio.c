/* SPDX-License-Identifier: MIT */
/*
 * hal_gpio.c - GPIO character device (uAPI v2) back end.
 */
#include "devhal/hal_gpio.h"
#include "hal_internal.h"

#include <fcntl.h>
#include <linux/gpio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MOD "gpio"

struct hal_gpio {
	int             fd;          /* line request fd */
	unsigned int    offset;
	hal_gpio_config_t cfg;
	pthread_mutex_t lock;
};

static int build_line_config(const hal_gpio_config_t *cfg,
			     struct gpio_v2_line_config *lc)
{
	uint64_t flags = 0;

	memset(lc, 0, sizeof(*lc));

	switch (cfg->direction) {
	case HAL_GPIO_DIR_INPUT:
		flags |= GPIO_V2_LINE_FLAG_INPUT;
		if (cfg->drive != HAL_GPIO_DRIVE_PUSH_PULL)
			return -EINVAL;
		break;
	case HAL_GPIO_DIR_OUTPUT:
		flags |= GPIO_V2_LINE_FLAG_OUTPUT;
		if (cfg->edge != HAL_GPIO_EDGE_NONE || cfg->debounce_us)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	switch (cfg->edge) {
	case HAL_GPIO_EDGE_NONE:
		break;
	case HAL_GPIO_EDGE_RISING:
		flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
		break;
	case HAL_GPIO_EDGE_FALLING:
		flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
		break;
	case HAL_GPIO_EDGE_BOTH:
		flags |= GPIO_V2_LINE_FLAG_EDGE_RISING |
			 GPIO_V2_LINE_FLAG_EDGE_FALLING;
		break;
	default:
		return -EINVAL;
	}

	switch (cfg->bias) {
	case HAL_GPIO_BIAS_AS_IS:
		break;
	case HAL_GPIO_BIAS_DISABLED:
		flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
		break;
	case HAL_GPIO_BIAS_PULL_UP:
		flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
		break;
	case HAL_GPIO_BIAS_PULL_DOWN:
		flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
		break;
	default:
		return -EINVAL;
	}

	switch (cfg->drive) {
	case HAL_GPIO_DRIVE_PUSH_PULL:
		break;
	case HAL_GPIO_DRIVE_OPEN_DRAIN:
		flags |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
		break;
	case HAL_GPIO_DRIVE_OPEN_SOURCE:
		flags |= GPIO_V2_LINE_FLAG_OPEN_SOURCE;
		break;
	default:
		return -EINVAL;
	}

	if (cfg->active_low)
		flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
	lc->flags = flags;

	if (cfg->direction == HAL_GPIO_DIR_OUTPUT) {
		struct gpio_v2_line_config_attribute *a =
			&lc->attrs[lc->num_attrs++];

		a->attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
		a->attr.values = cfg->initial_value ? 1 : 0;
		a->mask = 1;
	}
	if (cfg->debounce_us) {
		struct gpio_v2_line_config_attribute *a =
			&lc->attrs[lc->num_attrs++];

		a->attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
		a->attr.debounce_period_us = cfg->debounce_us;
		a->mask = 1;
	}
	return 0;
}

int hal_gpio_open(hal_gpio_t **out, const char *chip, unsigned int offset,
		  const hal_gpio_config_t *cfg)
{
	struct gpio_v2_line_request req;
	char path[HAL_PATH_MAX];
	hal_gpio_t *g;
	int chip_fd, r;

	if (!out || !cfg)
		return -EINVAL;
	*out = NULL;

	memset(&req, 0, sizeof(req));
	r = build_line_config(cfg, &req.config);
	if (r < 0)
		return r;
	r = hal_dev_path(path, sizeof(path), chip);
	if (r < 0)
		return r;

	req.offsets[0] = offset;
	req.num_lines = 1;
	strncpy(req.consumer, cfg->consumer ? cfg->consumer : "devhal",
		sizeof(req.consumer) - 1);

	chip_fd = hal_sys_open(path, O_RDWR);
	if (chip_fd < 0) {
		HAL_ERR(MOD, "open %s: %s", path, hal_strerror(chip_fd));
		return chip_fd;
	}
	r = hal_sys_ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req);
	/* The line fd stays valid after the chip fd is closed. */
	hal_sys_close(chip_fd);
	if (r < 0) {
		HAL_ERR(MOD, "request %s line %u: %s", path, offset,
			hal_strerror(r));
		return r;
	}

	g = calloc(1, sizeof(*g));
	if (!g) {
		hal_sys_close(req.fd);
		return -ENOMEM;
	}
	g->fd = req.fd;
	g->offset = offset;
	g->cfg = *cfg;
	g->cfg.consumer = NULL;  /* caller's string may not outlive us */
	pthread_mutex_init(&g->lock, NULL);
	*out = g;
	HAL_DBG(MOD, "%s line %u requested (fd %d)", path, offset, g->fd);
	return 0;
}

int hal_gpio_close(hal_gpio_t *g)
{
	int r;

	if (!g)
		return -EINVAL;
	r = hal_sys_close(g->fd);
	pthread_mutex_destroy(&g->lock);
	free(g);
	return r;
}

int hal_gpio_get(hal_gpio_t *g, int *value)
{
	struct gpio_v2_line_values v = { .bits = 0, .mask = 1 };
	int r;

	if (!g || !value)
		return -EINVAL;
	r = hal_sys_ioctl(g->fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v);
	if (r < 0)
		return r;
	*value = (int)(v.bits & 1);
	return 0;
}

static int gpio_set_locked(hal_gpio_t *g, int value)
{
	struct gpio_v2_line_values v = { .bits = value ? 1 : 0, .mask = 1 };

	if (g->cfg.direction != HAL_GPIO_DIR_OUTPUT)
		return -EPERM;
	return hal_sys_ioctl(g->fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

int hal_gpio_set(hal_gpio_t *g, int value)
{
	int r;

	if (!g)
		return -EINVAL;
	pthread_mutex_lock(&g->lock);
	r = gpio_set_locked(g, value);
	pthread_mutex_unlock(&g->lock);
	return r;
}

int hal_gpio_toggle(hal_gpio_t *g)
{
	int r, v = 0;

	if (!g)
		return -EINVAL;
	pthread_mutex_lock(&g->lock);
	r = hal_gpio_get(g, &v);
	if (r == 0)
		r = gpio_set_locked(g, !v);
	pthread_mutex_unlock(&g->lock);
	return r;
}

int hal_gpio_reconfigure(hal_gpio_t *g, const hal_gpio_config_t *cfg)
{
	struct gpio_v2_line_config lc;
	int r;

	if (!g || !cfg)
		return -EINVAL;
	r = build_line_config(cfg, &lc);
	if (r < 0)
		return r;
	pthread_mutex_lock(&g->lock);
	r = hal_sys_ioctl(g->fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &lc);
	if (r == 0) {
		g->cfg = *cfg;
		g->cfg.consumer = NULL;
	}
	pthread_mutex_unlock(&g->lock);
	return r;
}

int hal_gpio_wait_event(hal_gpio_t *g, int timeout_ms, hal_gpio_event_t *ev)
{
	struct gpio_v2_line_event raw;
	ssize_t n;
	int r;

	if (!g || !ev)
		return -EINVAL;
	if (g->cfg.edge == HAL_GPIO_EDGE_NONE)
		return -EPERM;

	r = hal_wait_fd(g->fd, POLLIN, timeout_ms);
	if (r < 0)
		return r;
	n = hal_sys_read(g->fd, &raw, sizeof(raw));
	if (n < 0)
		return (int)n;
	if ((size_t)n != sizeof(raw))
		return -EIO;

	ev->edge = raw.id == GPIO_V2_LINE_EVENT_RISING_EDGE ?
		   HAL_GPIO_EDGE_RISING : HAL_GPIO_EDGE_FALLING;
	ev->timestamp_ns = raw.timestamp_ns;
	ev->seqno = raw.line_seqno;
	return 0;
}

int hal_gpio_fd(const hal_gpio_t *g)
{
	return g ? g->fd : -EINVAL;
}
