/* SPDX-License-Identifier: MIT */
/*
 * hal_gpio.h - GPIO lines via the Linux GPIO character device (uAPI v2).
 *
 * Uses /dev/gpiochipN rather than the deprecated /sys/class/gpio interface.
 * A handle owns one requested line; the kernel releases it automatically if
 * the process dies.
 *
 *   hal_gpio_config_t cfg = HAL_GPIO_CONFIG_INIT;
 *   cfg.direction = HAL_GPIO_DIR_OUTPUT;
 *   hal_gpio_t *led;
 *   if (hal_gpio_open(&led, "gpiochip0", 17, &cfg) == 0) {
 *       hal_gpio_set(led, 1);
 *       hal_gpio_close(led);
 *   }
 */
#ifndef DEVHAL_HAL_GPIO_H
#define DEVHAL_HAL_GPIO_H

#include "hal_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_gpio hal_gpio_t;

typedef enum {
	HAL_GPIO_DIR_INPUT = 0,
	HAL_GPIO_DIR_OUTPUT,
} hal_gpio_dir_t;

typedef enum {
	HAL_GPIO_EDGE_NONE = 0,
	HAL_GPIO_EDGE_RISING,
	HAL_GPIO_EDGE_FALLING,
	HAL_GPIO_EDGE_BOTH,
} hal_gpio_edge_t;

typedef enum {
	HAL_GPIO_BIAS_AS_IS = 0,
	HAL_GPIO_BIAS_DISABLED,
	HAL_GPIO_BIAS_PULL_UP,
	HAL_GPIO_BIAS_PULL_DOWN,
} hal_gpio_bias_t;

typedef enum {
	HAL_GPIO_DRIVE_PUSH_PULL = 0,
	HAL_GPIO_DRIVE_OPEN_DRAIN,
	HAL_GPIO_DRIVE_OPEN_SOURCE,
} hal_gpio_drive_t;

typedef struct {
	hal_gpio_dir_t   direction;
	hal_gpio_edge_t  edge;          /* inputs only */
	hal_gpio_bias_t  bias;
	hal_gpio_drive_t drive;         /* outputs only */
	bool             active_low;
	int              initial_value; /* outputs only, logical 0/1 */
	uint32_t         debounce_us;   /* inputs only, 0 = off */
	const char      *consumer;      /* label shown by gpioinfo; NULL = "devhal" */
} hal_gpio_config_t;

#define HAL_GPIO_CONFIG_INIT \
	{ HAL_GPIO_DIR_INPUT, HAL_GPIO_EDGE_NONE, HAL_GPIO_BIAS_AS_IS, \
	  HAL_GPIO_DRIVE_PUSH_PULL, false, 0, 0, NULL }

typedef struct {
	hal_gpio_edge_t edge;       /* HAL_GPIO_EDGE_RISING or _FALLING */
	uint64_t        timestamp_ns;
	uint32_t        seqno;      /* per-line sequence number */
} hal_gpio_event_t;

/*
 * Request line @offset of @chip. @chip is either a device name
 * ("gpiochip0") or an absolute path ("/dev/gpiochip0").
 */
int hal_gpio_open(hal_gpio_t **out, const char *chip, unsigned int offset,
		  const hal_gpio_config_t *cfg);
int hal_gpio_close(hal_gpio_t *gpio);

/* Logical values: 1 = active, taking active_low into account. */
int hal_gpio_get(hal_gpio_t *gpio, int *value);
int hal_gpio_set(hal_gpio_t *gpio, int value);
int hal_gpio_toggle(hal_gpio_t *gpio);

/* Change direction / edge / bias etc. without releasing the line. */
int hal_gpio_reconfigure(hal_gpio_t *gpio, const hal_gpio_config_t *cfg);

/*
 * Wait for an edge event. timeout_ms < 0 waits forever, 0 polls.
 * Returns 0 with @ev filled, -ETIMEDOUT, or another negative error.
 */
int hal_gpio_wait_event(hal_gpio_t *gpio, int timeout_ms,
			hal_gpio_event_t *ev);

/* File descriptor for integrating with an external poll/epoll loop. */
int hal_gpio_fd(const hal_gpio_t *gpio);

#ifdef __cplusplus
}
#endif

#endif /* DEVHAL_HAL_GPIO_H */
