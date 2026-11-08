/* SPDX-License-Identifier: MIT */
/*
 * hal_internal.h - Helpers shared by devhal modules. Not installed.
 */
#ifndef DEVHAL_HAL_INTERNAL_H
#define DEVHAL_HAL_INTERNAL_H

#include "devhal/hal_core.h"

#define HAL_PATH_MAX 256

#if defined(__GNUC__)
#define HAL_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define HAL_PRINTF(a, b)
#endif

void hal_log(hal_log_level_t level, const char *module, const char *fmt, ...)
	HAL_PRINTF(3, 4);

#define HAL_ERR(mod, ...)  hal_log(HAL_LOG_ERROR, mod, __VA_ARGS__)
#define HAL_WARN(mod, ...) hal_log(HAL_LOG_WARN, mod, __VA_ARGS__)
#define HAL_INFO(mod, ...) hal_log(HAL_LOG_INFO, mod, __VA_ARGS__)
#define HAL_DBG(mod, ...)  hal_log(HAL_LOG_DEBUG, mod, __VA_ARGS__)

/*
 * System call wrappers routed through the active hal_sys_ops_t.
 * They retry on EINTR and return -errno on failure.
 */
int     hal_sys_open(const char *path, int flags);
int     hal_sys_close(int fd);
ssize_t hal_sys_read(int fd, void *buf, size_t len);
ssize_t hal_sys_write(int fd, const void *buf, size_t len);
int     hal_sys_ioctl(int fd, unsigned long request, void *arg);
int     hal_sys_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms);
int     hal_sys_tcgetattr(int fd, struct termios *tio);
int     hal_sys_tcsetattr(int fd, int actions, const struct termios *tio);
int     hal_sys_tcflush(int fd, int queue);
int     hal_sys_tcdrain(int fd);
int     hal_sys_access(const char *path, int mode);

/* Wait until @fd is ready for @events. 0, -ETIMEDOUT, or -errno. */
int hal_wait_fd(int fd, short events, int timeout_ms);

/* Monotonic clock in milliseconds. */
int64_t hal_now_ms(void);
/* Remaining time until @deadline_ms (>= 0), or -1 if @deadline_ms < 0. */
int hal_remaining_ms(int64_t deadline_ms);
void hal_sleep_us(unsigned int us);

/* "/dev/" + name unless @name is already absolute. */
int hal_dev_path(char *out, size_t len, const char *name);

/* sysfs helpers; @path is relative to the sysfs root ("/sys/..."). */
int hal_sysfs_path(char *out, size_t len, const char *fmt, ...) HAL_PRINTF(3, 4);
int hal_sysfs_read(const char *path, char *buf, size_t len);
int hal_sysfs_write(const char *path, const char *val);
int hal_sysfs_read_u64(const char *path, uint64_t *val);
int hal_sysfs_write_u64(const char *path, uint64_t val);

#endif /* DEVHAL_HAL_INTERNAL_H */
