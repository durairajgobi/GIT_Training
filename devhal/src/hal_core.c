/* SPDX-License-Identifier: MIT */
/*
 * hal_core.c - Error strings, logging, OS back end and shared helpers.
 */
#include "hal_internal.h"

#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define STR_(x) #x
#define STR(x) STR_(x)

const char *hal_version(void)
{
	return STR(DEVHAL_VERSION_MAJOR) "." STR(DEVHAL_VERSION_MINOR) "."
	       STR(DEVHAL_VERSION_PATCH);
}

const char *hal_strerror(int err)
{
	if (err >= 0)
		return "Success";
	switch (-err) {
	case ETIMEDOUT: return "Timed out";
	case ENXIO:     return "No device at address (NAK)";
	case EREMOTEIO: return "Remote I/O error (NAK)";
	default:        return strerror(-err);
	}
}

/* ------------------------------------------------------------------------ */
/* Logging                                                                  */
/* ------------------------------------------------------------------------ */

static void default_log(hal_log_level_t level, const char *module,
			const char *msg, void *user)
{
	static const char *const names[] = { "", "E", "W", "I", "D" };

	(void)user;
	fprintf(stderr, "devhal[%s] %s: %s\n", names[level], module, msg);
}

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static hal_log_fn log_fn = default_log;
static void *log_user;
static volatile hal_log_level_t log_level = HAL_LOG_WARN;
static pthread_once_t log_env_once = PTHREAD_ONCE_INIT;

static void log_env_init(void)
{
	const char *env = getenv("DEVHAL_LOG");

	if (!env)
		return;
	if (!strcmp(env, "none"))
		log_level = HAL_LOG_NONE;
	else if (!strcmp(env, "error"))
		log_level = HAL_LOG_ERROR;
	else if (!strcmp(env, "warn"))
		log_level = HAL_LOG_WARN;
	else if (!strcmp(env, "info"))
		log_level = HAL_LOG_INFO;
	else if (!strcmp(env, "debug"))
		log_level = HAL_LOG_DEBUG;
}

void hal_set_log_handler(hal_log_fn fn, void *user)
{
	pthread_mutex_lock(&log_lock);
	log_fn = fn ? fn : default_log;
	log_user = fn ? user : NULL;
	pthread_mutex_unlock(&log_lock);
}

void hal_set_log_level(hal_log_level_t level)
{
	pthread_once(&log_env_once, log_env_init);
	if (level > HAL_LOG_DEBUG)
		level = HAL_LOG_DEBUG;
	log_level = level;
}

hal_log_level_t hal_get_log_level(void)
{
	pthread_once(&log_env_once, log_env_init);
	return log_level;
}

void hal_log(hal_log_level_t level, const char *module, const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	if (level == HAL_LOG_NONE || level > hal_get_log_level())
		return;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	pthread_mutex_lock(&log_lock);
	log_fn(level, module, msg, log_user);
	pthread_mutex_unlock(&log_lock);
}

/* ------------------------------------------------------------------------ */
/* OS back end                                                              */
/* ------------------------------------------------------------------------ */

static int libc_open(const char *path, int flags, mode_t mode)
{
	return open(path, flags, mode);
}

static int libc_ioctl(int fd, unsigned long request, void *arg)
{
	return ioctl(fd, request, arg);
}

static const hal_sys_ops_t libc_ops = {
	.open      = libc_open,
	.close     = close,
	.read      = read,
	.write     = write,
	.ioctl     = libc_ioctl,
	.poll      = poll,
	.tcgetattr = tcgetattr,
	.tcsetattr = tcsetattr,
	.tcflush   = tcflush,
	.tcdrain   = tcdrain,
	.access    = access,
};

static hal_sys_ops_t ops = {
	.open      = libc_open,
	.close     = close,
	.read      = read,
	.write     = write,
	.ioctl     = libc_ioctl,
	.poll      = poll,
	.tcgetattr = tcgetattr,
	.tcsetattr = tcsetattr,
	.tcflush   = tcflush,
	.tcdrain   = tcdrain,
	.access    = access,
};

#define FILL(m) ops.m = (o && o->m) ? o->m : libc_ops.m

void hal_set_sys_ops(const hal_sys_ops_t *o)
{
	FILL(open);
	FILL(close);
	FILL(read);
	FILL(write);
	FILL(ioctl);
	FILL(poll);
	FILL(tcgetattr);
	FILL(tcsetattr);
	FILL(tcflush);
	FILL(tcdrain);
	FILL(access);
}

#undef FILL

int hal_sys_open(const char *path, int flags)
{
	int r;

	do {
		r = ops.open(path, flags | O_CLOEXEC, 0);
	} while (r < 0 && errno == EINTR);
	return r < 0 ? -errno : r;
}

int hal_sys_close(int fd)
{
	/* Never retry close(): on Linux the fd is gone even on EINTR. */
	return ops.close(fd) < 0 && errno != EINTR ? -errno : 0;
}

ssize_t hal_sys_read(int fd, void *buf, size_t len)
{
	ssize_t r;

	do {
		r = ops.read(fd, buf, len);
	} while (r < 0 && errno == EINTR);
	return r < 0 ? -errno : r;
}

ssize_t hal_sys_write(int fd, const void *buf, size_t len)
{
	ssize_t r;

	do {
		r = ops.write(fd, buf, len);
	} while (r < 0 && errno == EINTR);
	return r < 0 ? -errno : r;
}

int hal_sys_ioctl(int fd, unsigned long request, void *arg)
{
	int r;

	do {
		r = ops.ioctl(fd, request, arg);
	} while (r < 0 && errno == EINTR);
	return r < 0 ? -errno : r;
}

int hal_sys_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms)
{
	int r = ops.poll(fds, nfds, timeout_ms);

	/* EINTR is reported as "nothing ready" so callers recompute timeouts. */
	if (r < 0)
		return errno == EINTR ? 0 : -errno;
	return r;
}

int hal_sys_tcgetattr(int fd, struct termios *tio)
{
	int r;

	do {
		r = ops.tcgetattr(fd, tio);
	} while (r < 0 && errno == EINTR);
	return r < 0 ? -errno : 0;
}

int hal_sys_tcsetattr(int fd, int actions, const struct termios *tio)
{
	int r;

	do {
		r = ops.tcsetattr(fd, actions, tio);
	} while (r < 0 && errno == EINTR);
	return r < 0 ? -errno : 0;
}

int hal_sys_tcflush(int fd, int queue)
{
	return ops.tcflush(fd, queue) < 0 ? -errno : 0;
}

int hal_sys_tcdrain(int fd)
{
	int r;

	do {
		r = ops.tcdrain(fd);
	} while (r < 0 && errno == EINTR);
	return r < 0 ? -errno : 0;
}

int hal_sys_access(const char *path, int mode)
{
	return ops.access(path, mode) < 0 ? -errno : 0;
}

/* ------------------------------------------------------------------------ */
/* Time and waiting                                                         */
/* ------------------------------------------------------------------------ */

int64_t hal_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int hal_remaining_ms(int64_t deadline_ms)
{
	int64_t left;

	if (deadline_ms < 0)
		return -1;
	left = deadline_ms - hal_now_ms();
	if (left < 0)
		return 0;
	return left > INT32_MAX ? INT32_MAX : (int)left;
}

void hal_sleep_us(unsigned int us)
{
	struct timespec ts = { us / 1000000u, (long)(us % 1000000u) * 1000L };

	while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
}

int hal_wait_fd(int fd, short events, int timeout_ms)
{
	int64_t deadline = timeout_ms < 0 ? -1 : hal_now_ms() + timeout_ms;

	for (;;) {
		struct pollfd p = { .fd = fd, .events = events };
		int r = hal_sys_poll(&p, 1, hal_remaining_ms(deadline));

		if (r < 0)
			return r;
		if (r > 0) {
			if (p.revents & (POLLERR | POLLNVAL))
				return p.revents & POLLNVAL ? -EBADF : -EIO;
			if (p.revents & (events | POLLHUP))
				return 0;
		}
		if (deadline >= 0 && hal_remaining_ms(deadline) == 0)
			return -ETIMEDOUT;
	}
}

/* ------------------------------------------------------------------------ */
/* Paths and sysfs                                                          */
/* ------------------------------------------------------------------------ */

int hal_dev_path(char *out, size_t len, const char *name)
{
	int n;

	if (!name || !*name)
		return -EINVAL;
	n = snprintf(out, len, "%s%s", name[0] == '/' ? "" : "/dev/", name);
	return n < 0 || (size_t)n >= len ? -ENAMETOOLONG : 0;
}

static char sysfs_root[HAL_PATH_MAX];

int hal_set_sysfs_root(const char *root)
{
	size_t n = root ? strlen(root) : 0;

	if (n >= sizeof(sysfs_root))
		return -ENAMETOOLONG;
	memcpy(sysfs_root, root ? root : "", n);
	/* Strip a trailing '/', paths below always start with one. */
	while (n > 0 && sysfs_root[n - 1] == '/')
		n--;
	sysfs_root[n] = '\0';
	return 0;
}

const char *hal_get_sysfs_root(void)
{
	return sysfs_root;
}

int hal_sysfs_path(char *out, size_t len, const char *fmt, ...)
{
	va_list ap;
	size_t rl = strlen(sysfs_root);
	int n;

	if (rl >= len)
		return -ENAMETOOLONG;
	memcpy(out, sysfs_root, rl);
	va_start(ap, fmt);
	n = vsnprintf(out + rl, len - rl, fmt, ap);
	va_end(ap);
	return n < 0 || (size_t)n >= len - rl ? -ENAMETOOLONG : 0;
}

int hal_sysfs_read(const char *path, char *buf, size_t len)
{
	ssize_t n;
	int fd;

	if (len == 0)
		return -EINVAL;
	fd = hal_sys_open(path, O_RDONLY);
	if (fd < 0)
		return fd;
	n = hal_sys_read(fd, buf, len - 1);
	hal_sys_close(fd);
	if (n < 0)
		return (int)n;
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
		n--;
	buf[n] = '\0';
	return (int)n;
}

int hal_sysfs_write(const char *path, const char *val)
{
	size_t len = strlen(val);
	ssize_t n;
	int fd;

	/* O_TRUNC is ignored by sysfs but lets tests use regular files. */
	fd = hal_sys_open(path, O_WRONLY | O_TRUNC);
	if (fd < 0)
		return fd;
	n = hal_sys_write(fd, val, len);
	hal_sys_close(fd);
	if (n < 0) {
		HAL_DBG("sysfs", "write '%s' to %s: %s", val, path,
			hal_strerror((int)n));
		return (int)n;
	}
	return (size_t)n == len ? 0 : -EIO;
}

int hal_sysfs_read_u64(const char *path, uint64_t *val)
{
	char buf[32], *end;
	unsigned long long v;
	int r = hal_sysfs_read(path, buf, sizeof(buf));

	if (r < 0)
		return r;
	errno = 0;
	v = strtoull(buf, &end, 0);
	if (errno || end == buf)
		return -EPROTO;
	*val = v;
	return 0;
}

int hal_sysfs_write_u64(const char *path, uint64_t val)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%" PRIu64, val);
	return hal_sysfs_write(path, buf);
}
