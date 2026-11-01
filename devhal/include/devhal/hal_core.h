/* SPDX-License-Identifier: MIT */
/*
 * hal_core.h - devhal common definitions: error model, logging, version and
 *              the pluggable OS back end.
 *
 * Error model
 * -----------
 * Every devhal function that can fail returns an int:
 *
 *   >= 0   success (some calls return a byte count)
 *   <  0   a negated errno value, e.g. -ENODEV, -ETIMEDOUT, -EINVAL
 *
 * hal_strerror() turns the code into text. errno itself is never relied on
 * by callers, so the library is safe to use from several threads.
 *
 * OS back end
 * -----------
 * All system calls go through a table of function pointers (hal_sys_ops_t).
 * Production code uses the default libc table; unit tests, simulators and
 * HIL rigs install their own with hal_set_sys_ops() and can then exercise
 * the middleware without real hardware.
 */
#ifndef DEVHAL_HAL_CORE_H
#define DEVHAL_HAL_CORE_H

#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVHAL_VERSION_MAJOR 1
#define DEVHAL_VERSION_MINOR 0
#define DEVHAL_VERSION_PATCH 0

/* Returns "major.minor.patch" of the library actually linked. */
const char *hal_version(void);

/* Human-readable text for a devhal return code (0 or -errno). */
const char *hal_strerror(int err);

/* ------------------------------------------------------------------------ */
/* Logging                                                                  */
/* ------------------------------------------------------------------------ */

typedef enum {
	HAL_LOG_NONE = 0,
	HAL_LOG_ERROR,
	HAL_LOG_WARN,
	HAL_LOG_INFO,
	HAL_LOG_DEBUG,
} hal_log_level_t;

typedef void (*hal_log_fn)(hal_log_level_t level, const char *module,
			   const char *msg, void *user);

/*
 * Install a log sink. NULL restores the default sink (stderr).
 * Messages above the current level are dropped before formatting.
 */
void hal_set_log_handler(hal_log_fn fn, void *user);
void hal_set_log_level(hal_log_level_t level);
hal_log_level_t hal_get_log_level(void);

/* ------------------------------------------------------------------------ */
/* OS back end                                                              */
/* ------------------------------------------------------------------------ */

/*
 * Each member follows the libc contract (returns -1 and sets errno on
 * failure). Any member left NULL falls back to the libc implementation, so a
 * test double only needs to override what it simulates.
 */
typedef struct hal_sys_ops {
	int     (*open)(const char *path, int flags, mode_t mode);
	int     (*close)(int fd);
	ssize_t (*read)(int fd, void *buf, size_t len);
	ssize_t (*write)(int fd, const void *buf, size_t len);
	int     (*ioctl)(int fd, unsigned long request, void *arg);
	int     (*poll)(struct pollfd *fds, nfds_t nfds, int timeout_ms);
	int     (*tcgetattr)(int fd, struct termios *tio);
	int     (*tcsetattr)(int fd, int actions, const struct termios *tio);
	int     (*tcflush)(int fd, int queue);
	int     (*tcdrain)(int fd);
	int     (*access)(const char *path, int mode);
} hal_sys_ops_t;

/*
 * Replace the OS back end. NULL restores libc. The table is copied, so the
 * caller's struct need not outlive the call. Not meant to be switched while
 * devices are open.
 */
void hal_set_sys_ops(const hal_sys_ops_t *ops);

/*
 * Prefix prepended to every /sys path (default ""). Lets tests point the
 * sysfs-based modules at a scratch directory tree.
 */
int hal_set_sysfs_root(const char *root);
const char *hal_get_sysfs_root(void);

#ifdef __cplusplus
}
#endif

#endif /* DEVHAL_HAL_CORE_H */
