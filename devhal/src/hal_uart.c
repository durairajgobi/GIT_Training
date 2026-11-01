/* SPDX-License-Identifier: MIT */
/*
 * hal_uart.c - Raw-mode serial ports with explicit timeouts.
 */
#include "devhal/hal_uart.h"
#include "hal_internal.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MOD "uart"

struct hal_uart {
	int fd;
	pthread_mutex_t rx_lock;  /* one reader at a time keeps frames whole */
	pthread_mutex_t tx_lock;  /* one writer at a time keeps frames whole */
};

static const struct {
	uint32_t baud;
	speed_t  speed;
} baud_table[] = {
	{ 50, B50 }, { 75, B75 }, { 110, B110 }, { 134, B134 },
	{ 150, B150 }, { 200, B200 }, { 300, B300 }, { 600, B600 },
	{ 1200, B1200 }, { 1800, B1800 }, { 2400, B2400 }, { 4800, B4800 },
	{ 9600, B9600 }, { 19200, B19200 }, { 38400, B38400 },
	{ 57600, B57600 }, { 115200, B115200 }, { 230400, B230400 },
#ifdef B460800
	{ 460800, B460800 },
#endif
#ifdef B500000
	{ 500000, B500000 },
#endif
#ifdef B576000
	{ 576000, B576000 },
#endif
#ifdef B921600
	{ 921600, B921600 },
#endif
#ifdef B1000000
	{ 1000000, B1000000 },
#endif
#ifdef B1152000
	{ 1152000, B1152000 },
#endif
#ifdef B1500000
	{ 1500000, B1500000 },
#endif
#ifdef B2000000
	{ 2000000, B2000000 },
#endif
#ifdef B2500000
	{ 2500000, B2500000 },
#endif
#ifdef B3000000
	{ 3000000, B3000000 },
#endif
#ifdef B3500000
	{ 3500000, B3500000 },
#endif
#ifdef B4000000
	{ 4000000, B4000000 },
#endif
};

static int baud_to_speed(uint32_t baud, speed_t *out)
{
	size_t i;

	for (i = 0; i < sizeof(baud_table) / sizeof(baud_table[0]); i++) {
		if (baud_table[i].baud == baud) {
			*out = baud_table[i].speed;
			return 0;
		}
	}
	return -EINVAL;
}

static int uart_apply(int fd, const hal_uart_config_t *cfg)
{
	struct termios tio;
	speed_t speed;
	int r;

	if (cfg->data_bits < 5 || cfg->data_bits > 8 ||
	    (cfg->stop_bits != 1 && cfg->stop_bits != 2) ||
	    cfg->parity > HAL_UART_PARITY_EVEN)
		return -EINVAL;
	r = baud_to_speed(cfg->baud, &speed);
	if (r < 0) {
		HAL_ERR(MOD, "unsupported baud rate %u", cfg->baud);
		return r;
	}

	r = hal_sys_tcgetattr(fd, &tio);
	if (r < 0)
		return r;

	/* Raw mode, equivalent to cfmakeraw(). */
	tio.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
				   IGNCR | ICRNL | IXON | IXOFF | IXANY);
	tio.c_oflag &= ~(tcflag_t)OPOST;
	tio.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tio.c_cflag &= ~(tcflag_t)(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
	tio.c_cflag |= CLOCAL | CREAD;

	switch (cfg->data_bits) {
	case 5: tio.c_cflag |= CS5; break;
	case 6: tio.c_cflag |= CS6; break;
	case 7: tio.c_cflag |= CS7; break;
	default: tio.c_cflag |= CS8; break;
	}
	if (cfg->parity != HAL_UART_PARITY_NONE) {
		tio.c_cflag |= PARENB;
		tio.c_iflag |= INPCK;
		if (cfg->parity == HAL_UART_PARITY_ODD)
			tio.c_cflag |= PARODD;
	} else {
		tio.c_iflag &= ~(tcflag_t)INPCK;
	}
	if (cfg->stop_bits == 2)
		tio.c_cflag |= CSTOPB;
	if (cfg->rtscts)
		tio.c_cflag |= CRTSCTS;

	/* Non-blocking fd + poll() provides timeouts, so VMIN/VTIME = 0. */
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;
	cfsetispeed(&tio, speed);
	cfsetospeed(&tio, speed);

	return hal_sys_tcsetattr(fd, TCSANOW, &tio);
}

int hal_uart_open(hal_uart_t **out, const char *path,
		  const hal_uart_config_t *cfg)
{
	hal_uart_config_t def = HAL_UART_CONFIG_INIT;
	hal_uart_t *u;
	int fd, r;

	if (!out || !path)
		return -EINVAL;
	*out = NULL;
	if (!cfg)
		cfg = &def;

	fd = hal_sys_open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		HAL_ERR(MOD, "open %s: %s", path, hal_strerror(fd));
		return fd;
	}
	r = uart_apply(fd, cfg);
	if (r == 0)
		r = hal_sys_tcflush(fd, TCIOFLUSH);
	if (r < 0) {
		HAL_ERR(MOD, "configure %s: %s", path, hal_strerror(r));
		hal_sys_close(fd);
		return r;
	}
	u = calloc(1, sizeof(*u));
	if (!u) {
		hal_sys_close(fd);
		return -ENOMEM;
	}
	u->fd = fd;
	pthread_mutex_init(&u->rx_lock, NULL);
	pthread_mutex_init(&u->tx_lock, NULL);
	*out = u;
	return 0;
}

int hal_uart_close(hal_uart_t *u)
{
	int r;

	if (!u)
		return -EINVAL;
	r = hal_sys_close(u->fd);
	pthread_mutex_destroy(&u->rx_lock);
	pthread_mutex_destroy(&u->tx_lock);
	free(u);
	return r;
}

int hal_uart_configure(hal_uart_t *u, const hal_uart_config_t *cfg)
{
	int r;

	if (!u || !cfg)
		return -EINVAL;
	pthread_mutex_lock(&u->tx_lock);
	pthread_mutex_lock(&u->rx_lock);
	r = uart_apply(u->fd, cfg);
	pthread_mutex_unlock(&u->rx_lock);
	pthread_mutex_unlock(&u->tx_lock);
	return r;
}

ssize_t hal_uart_write(hal_uart_t *u, const void *buf, size_t len,
		       int timeout_ms)
{
	const uint8_t *p = buf;
	int64_t deadline;
	size_t done = 0;
	ssize_t r = 0;

	if (!u || (len && !buf))
		return -EINVAL;
	deadline = timeout_ms < 0 ? -1 : hal_now_ms() + timeout_ms;

	pthread_mutex_lock(&u->tx_lock);
	while (done < len) {
		r = hal_sys_write(u->fd, p + done, len - done);
		if (r > 0) {
			done += (size_t)r;
			continue;
		}
		if (r < 0 && r != -EAGAIN)
			break;
		r = hal_wait_fd(u->fd, POLLOUT, hal_remaining_ms(deadline));
		if (r < 0)
			break;
	}
	pthread_mutex_unlock(&u->tx_lock);
	return done == len ? (ssize_t)len : r;
}

/* Caller holds rx_lock. */
static ssize_t uart_read_some(hal_uart_t *u, void *buf, size_t len,
			      int64_t deadline)
{
	for (;;) {
		ssize_t r = hal_sys_read(u->fd, buf, len);

		if (r > 0)
			return r;
		if (r < 0 && r != -EAGAIN)
			return r;
		/* r == 0 on a tty in non-blocking raw mode means "no data". */
		r = hal_wait_fd(u->fd, POLLIN, hal_remaining_ms(deadline));
		if (r < 0)
			return r;
	}
}

ssize_t hal_uart_read(hal_uart_t *u, void *buf, size_t len, int timeout_ms)
{
	ssize_t r;

	if (!u || !buf || len == 0)
		return -EINVAL;
	pthread_mutex_lock(&u->rx_lock);
	r = uart_read_some(u, buf, len,
			   timeout_ms < 0 ? -1 : hal_now_ms() + timeout_ms);
	pthread_mutex_unlock(&u->rx_lock);
	return r;
}

ssize_t hal_uart_read_exact(hal_uart_t *u, void *buf, size_t len,
			    int timeout_ms)
{
	uint8_t *p = buf;
	int64_t deadline;
	size_t done = 0;
	ssize_t r = 0;

	if (!u || !buf || len == 0)
		return -EINVAL;
	deadline = timeout_ms < 0 ? -1 : hal_now_ms() + timeout_ms;

	pthread_mutex_lock(&u->rx_lock);
	while (done < len) {
		r = uart_read_some(u, p + done, len - done, deadline);
		if (r < 0)
			break;
		done += (size_t)r;
	}
	pthread_mutex_unlock(&u->rx_lock);
	return done == len ? (ssize_t)len : r;
}

ssize_t hal_uart_read_line(hal_uart_t *u, char *buf, size_t len, char delim,
			   int timeout_ms)
{
	int64_t deadline;
	size_t n = 0;
	ssize_t r;

	if (!u || !buf || len < 2)
		return -EINVAL;
	deadline = timeout_ms < 0 ? -1 : hal_now_ms() + timeout_ms;

	/*
	 * Byte-at-a-time so nothing after the delimiter is consumed; serial
	 * line rates make the syscall overhead irrelevant.
	 */
	pthread_mutex_lock(&u->rx_lock);
	for (;;) {
		if (n == len - 1) {
			r = -ENOBUFS;
			break;
		}
		r = uart_read_some(u, buf + n, 1, deadline);
		if (r < 0)
			break;
		if (buf[n++] == delim) {
			r = (ssize_t)n;
			break;
		}
	}
	pthread_mutex_unlock(&u->rx_lock);
	buf[n] = '\0';
	return r;
}

int hal_uart_flush(hal_uart_t *u, hal_uart_flush_t which)
{
	int q;

	if (!u)
		return -EINVAL;
	switch (which) {
	case HAL_UART_FLUSH_RX:   q = TCIFLUSH; break;
	case HAL_UART_FLUSH_TX:   q = TCOFLUSH; break;
	case HAL_UART_FLUSH_BOTH: q = TCIOFLUSH; break;
	default: return -EINVAL;
	}
	return hal_sys_tcflush(u->fd, q);
}

int hal_uart_drain(hal_uart_t *u)
{
	return u ? hal_sys_tcdrain(u->fd) : -EINVAL;
}

int hal_uart_fd(const hal_uart_t *u)
{
	return u ? u->fd : -EINVAL;
}
