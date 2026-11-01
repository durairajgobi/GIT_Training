/* SPDX-License-Identifier: MIT */
/*
 * mock_sys.c - Simulated kernel drivers behind hal_sys_ops_t.
 */
#include "mock_sys.h"

#include <errno.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <sys/ioctl.h>

#include "pwm_lcd_bl.h"

struct mock_state mock;

enum kind { K_FREE = 0, K_I2C, K_SPI, K_CHIP, K_LINE, K_UART, K_BL };

#define FD_BASE 1000
#define MAX_FDS 16
static enum kind fds[MAX_FDS];

void mock_reset(void)
{
	memset(&mock, 0, sizeof(mock));
	memset(fds, 0, sizeof(fds));
	mock.i2c_funcs = I2C_FUNC_I2C;
	mock.uart_write_chunk = sizeof(mock.uart_tx);
	mock.bl_period = 1000000;
	mock.bl_max = 255;
}

static int alloc_fd(enum kind k)
{
	int i;

	for (i = 0; i < MAX_FDS; i++) {
		if (fds[i] == K_FREE) {
			fds[i] = k;
			mock.open_fds++;
			return FD_BASE + i;
		}
	}
	errno = EMFILE;
	return -1;
}

static enum kind kind_of(int fd)
{
	if (fd < FD_BASE || fd >= FD_BASE + MAX_FDS)
		return K_FREE;
	return fds[fd - FD_BASE];
}

static int m_open(const char *path, int flags, mode_t mode)
{
	(void)flags;
	(void)mode;
	if (!strcmp(path, "/dev/i2c-1"))
		return alloc_fd(K_I2C);
	if (!strcmp(path, "/dev/spidev0.0"))
		return alloc_fd(K_SPI);
	if (!strcmp(path, "/dev/gpiochip0"))
		return alloc_fd(K_CHIP);
	if (!strcmp(path, "/dev/ttyMOCK0"))
		return alloc_fd(K_UART);
	if (!strcmp(path, "/dev/lcd_backlight"))
		return alloc_fd(K_BL);
	errno = ENOENT;
	return -1;
}

static int m_close(int fd)
{
	if (kind_of(fd) == K_FREE) {
		errno = EBADF;
		return -1;
	}
	fds[fd - FD_BASE] = K_FREE;
	mock.open_fds--;
	return 0;
}

/* ---------------------------------------------------------------- I2C --- */

static int i2c_ioctl(unsigned long req, void *arg)
{
	struct i2c_rdwr_ioctl_data *d = arg;
	uint32_t i, j;

	if (req == I2C_FUNCS) {
		*(unsigned long *)arg = mock.i2c_funcs;
		return 0;
	}
	if (req != I2C_RDWR) {
		errno = ENOTTY;
		return -1;
	}
	mock.i2c_xfers++;
	if (mock.i2c_fail_next > 0) {
		mock.i2c_fail_next--;
		errno = EAGAIN;
		return -1;
	}
	for (i = 0; i < d->nmsgs; i++) {
		struct i2c_msg *m = &d->msgs[i];

		if (m->addr != MOCK_I2C_ADDR) {
			errno = ENXIO;
			return -1;
		}
		if (m->flags & I2C_M_RD) {
			for (j = 0; j < m->len; j++)
				m->buf[j] = mock.i2c_regs[mock.i2c_ptr++];
		} else if (m->len) {
			mock.i2c_ptr = m->buf[0];
			for (j = 1; j < m->len; j++)
				mock.i2c_regs[mock.i2c_ptr++] = m->buf[j];
		}
	}
	return (int)d->nmsgs;
}

/* ---------------------------------------------------------------- SPI --- */

static int spi_ioctl(unsigned long req, void *arg)
{
	if (_IOC_TYPE(req) == SPI_IOC_MAGIC && _IOC_NR(req) == 0 &&
	    _IOC_DIR(req) == _IOC_WRITE) {
		struct spi_ioc_transfer *t = arg;
		size_t n = _IOC_SIZE(req) / sizeof(*t), i;
		int total = 0;

		mock.spi_msgs++;
		mock.spi_last_nxfers = (int)n;
		mock.spi_tx_len = 0;
		for (i = 0; i < n; i++) {
			const uint8_t *tx = (const uint8_t *)(uintptr_t)t[i].tx_buf;
			uint8_t *rx = (uint8_t *)(uintptr_t)t[i].rx_buf;
			uint32_t k;

			for (k = 0; k < t[i].len; k++) {
				uint8_t b = tx ? tx[k] : 0xFF;

				if (mock.spi_tx_len < sizeof(mock.spi_tx))
					mock.spi_tx[mock.spi_tx_len++] = b;
				if (rx)
					rx[k] = tx ? b : 0xA5;
			}
			total += (int)t[i].len;
		}
		return total;
	}
	switch (req) {
	case SPI_IOC_WR_MODE:
		mock.spi_mode = *(uint8_t *)arg;
		return 0;
	case SPI_IOC_WR_BITS_PER_WORD:
		mock.spi_bits = *(uint8_t *)arg;
		return 0;
	case SPI_IOC_WR_MAX_SPEED_HZ:
		mock.spi_speed = *(uint32_t *)arg;
		return 0;
	case SPI_IOC_WR_LSB_FIRST:
		return 0;
	}
	errno = ENOTTY;
	return -1;
}

/* --------------------------------------------------------------- GPIO --- */

static void gpio_record_config(const struct gpio_v2_line_config *c)
{
	uint32_t i;

	mock.gpio_flags = c->flags;
	mock.gpio_debounce = 0;
	for (i = 0; i < c->num_attrs; i++) {
		const struct gpio_v2_line_attribute *a = &c->attrs[i].attr;

		if (a->id == GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES)
			mock.gpio_out_value = (int)(a->values & 1);
		else if (a->id == GPIO_V2_LINE_ATTR_ID_DEBOUNCE)
			mock.gpio_debounce = a->debounce_period_us;
	}
}

static int chip_ioctl(unsigned long req, void *arg)
{
	struct gpio_v2_line_request *r = arg;

	if (req != GPIO_V2_GET_LINE_IOCTL) {
		errno = ENOTTY;
		return -1;
	}
	if (r->num_lines != 1 || r->offsets[0] >= 32) {
		errno = EINVAL;
		return -1;
	}
	mock.gpio_offset = r->offsets[0];
	memcpy(mock.gpio_consumer, r->consumer, sizeof(mock.gpio_consumer));
	gpio_record_config(&r->config);
	r->fd = alloc_fd(K_LINE);
	return r->fd < 0 ? -1 : 0;
}

static int line_ioctl(unsigned long req, void *arg)
{
	struct gpio_v2_line_values *v = arg;

	switch (req) {
	case GPIO_V2_LINE_GET_VALUES_IOCTL:
		v->bits = (mock.gpio_flags & GPIO_V2_LINE_FLAG_OUTPUT) ?
			  (uint64_t)mock.gpio_out_value :
			  (uint64_t)mock.gpio_in_value;
		return 0;
	case GPIO_V2_LINE_SET_VALUES_IOCTL:
		mock.gpio_out_value = (int)(v->bits & 1);
		return 0;
	case GPIO_V2_LINE_SET_CONFIG_IOCTL:
		gpio_record_config(arg);
		return 0;
	}
	errno = ENOTTY;
	return -1;
}

void mock_gpio_push_event(uint32_t id)
{
	if (mock.gpio_ev_count < 8)
		mock.gpio_ev_id[mock.gpio_ev_count++] = id;
}

/* ---------------------------------------------------------- Backlight --- */

static uint32_t bl_brightness(void)
{
	return (uint32_t)(mock.bl_duty * mock.bl_max / mock.bl_period);
}

static int bl_ioctl(unsigned long req, void *arg)
{
	struct lcd_bl_config *c = arg;
	uint32_t level;

	switch (req) {
	case LCD_BL_IOC_ENABLE:
		mock.bl_enabled = 1;
		return 0;
	case LCD_BL_IOC_DISABLE:
		mock.bl_enabled = 0;
		return 0;
	case LCD_BL_IOC_GET_CONFIG:
		c->period_ns = mock.bl_period;
		c->duty_ns = mock.bl_duty;
		c->polarity = mock.bl_polarity;
		c->enabled = mock.bl_enabled;
		c->brightness = bl_brightness();
		c->max_brightness = mock.bl_max;
		return 0;
	case LCD_BL_IOC_SET_CONFIG:
		if (!c->period_ns || c->duty_ns > c->period_ns ||
		    c->polarity > LCD_BL_POLARITY_INVERSED) {
			errno = EINVAL;
			return -1;
		}
		mock.bl_period = c->period_ns;
		mock.bl_duty = c->duty_ns;
		mock.bl_polarity = c->polarity;
		return 0;
	case LCD_BL_IOC_SET_BRIGHTNESS:
		level = *(uint32_t *)arg;
		if (level > mock.bl_max) {
			errno = EINVAL;
			return -1;
		}
		mock.bl_set_brightness_calls++;
		/* Round up so brightness reads back exactly. */
		mock.bl_duty = (mock.bl_period * level + mock.bl_max - 1) /
			       mock.bl_max;
		return 0;
	}
	errno = ENOTTY;
	return -1;
}

/* ------------------------------------------------------------- common --- */

static int m_ioctl(int fd, unsigned long req, void *arg)
{
	switch (kind_of(fd)) {
	case K_I2C:  return i2c_ioctl(req, arg);
	case K_SPI:  return spi_ioctl(req, arg);
	case K_CHIP: return chip_ioctl(req, arg);
	case K_LINE: return line_ioctl(req, arg);
	case K_BL:   return bl_ioctl(req, arg);
	default:
		errno = kind_of(fd) == K_FREE ? EBADF : ENOTTY;
		return -1;
	}
}

static ssize_t m_read(int fd, void *buf, size_t len)
{
	switch (kind_of(fd)) {
	case K_LINE: {
		struct gpio_v2_line_event ev;
		static uint32_t seq;

		if (mock.gpio_ev_count == 0) {
			errno = EAGAIN;
			return -1;
		}
		if (len < sizeof(ev)) {
			errno = EINVAL;
			return -1;
		}
		memset(&ev, 0, sizeof(ev));
		ev.id = mock.gpio_ev_id[0];
		ev.offset = mock.gpio_offset;
		ev.timestamp_ns = 1000000ull * (++seq);
		ev.line_seqno = seq;
		memmove(mock.gpio_ev_id, mock.gpio_ev_id + 1,
			sizeof(mock.gpio_ev_id[0]) * (size_t)--mock.gpio_ev_count);
		memcpy(buf, &ev, sizeof(ev));
		return (ssize_t)sizeof(ev);
	}
	case K_UART: {
		size_t n = len < mock.uart_rx_len ? len : mock.uart_rx_len;

		if (n == 0) {
			errno = EAGAIN;
			return -1;
		}
		memcpy(buf, mock.uart_rx, n);
		memmove(mock.uart_rx, mock.uart_rx + n, mock.uart_rx_len - n);
		mock.uart_rx_len -= n;
		return (ssize_t)n;
	}
	default:
		errno = EBADF;
		return -1;
	}
}

static ssize_t m_write(int fd, const void *buf, size_t len)
{
	size_t room, n;

	if (kind_of(fd) != K_UART) {
		errno = EBADF;
		return -1;
	}
	room = sizeof(mock.uart_tx) - mock.uart_tx_len;
	n = len;
	if (n > mock.uart_write_chunk)
		n = mock.uart_write_chunk;
	if (n > room)
		n = room;
	if (mock.uart_tx_full || n == 0) {
		errno = EAGAIN;
		return -1;
	}
	memcpy(mock.uart_tx + mock.uart_tx_len, buf, n);
	mock.uart_tx_len += n;
	return (ssize_t)n;
}

static int m_poll(struct pollfd *pfd, nfds_t n, int timeout_ms)
{
	nfds_t i;
	int ready = 0;

	(void)timeout_ms;   /* a simulated device never becomes ready later */
	for (i = 0; i < n; i++) {
		pfd[i].revents = 0;
		switch (kind_of(pfd[i].fd)) {
		case K_LINE:
			if (mock.gpio_ev_count)
				pfd[i].revents |= POLLIN;
			break;
		case K_UART:
			if (mock.uart_rx_len)
				pfd[i].revents |= POLLIN;
			if (!mock.uart_tx_full)
				pfd[i].revents |= POLLOUT;
			break;
		case K_FREE:
			pfd[i].revents = POLLNVAL;
			break;
		default:
			break;
		}
		pfd[i].revents &= (short)(pfd[i].events | POLLNVAL);
		if (pfd[i].revents)
			ready++;
	}
	return ready;
}

static int m_tcgetattr(int fd, struct termios *t)
{
	if (kind_of(fd) != K_UART) {
		errno = ENOTTY;
		return -1;
	}
	*t = mock.tio;
	return 0;
}

static int m_tcsetattr(int fd, int act, const struct termios *t)
{
	(void)act;
	if (kind_of(fd) != K_UART) {
		errno = ENOTTY;
		return -1;
	}
	mock.tio = *t;
	return 0;
}

static int m_tcflush(int fd, int q)
{
	if (kind_of(fd) != K_UART) {
		errno = ENOTTY;
		return -1;
	}
	if (q == TCIFLUSH || q == TCIOFLUSH)
		mock.uart_rx_len = 0;
	mock.uart_flushes++;
	return 0;
}

static int m_tcdrain(int fd)
{
	return kind_of(fd) == K_UART ? 0 : (errno = ENOTTY, -1);
}

void mock_uart_feed(const void *data, size_t len)
{
	if (len > sizeof(mock.uart_rx) - mock.uart_rx_len)
		len = sizeof(mock.uart_rx) - mock.uart_rx_len;
	memcpy(mock.uart_rx + mock.uart_rx_len, data, len);
	mock.uart_rx_len += len;
}

const hal_sys_ops_t mock_ops = {
	.open      = m_open,
	.close     = m_close,
	.read      = m_read,
	.write     = m_write,
	.ioctl     = m_ioctl,
	.poll      = m_poll,
	.tcgetattr = m_tcgetattr,
	.tcsetattr = m_tcsetattr,
	.tcflush   = m_tcflush,
	.tcdrain   = m_tcdrain,
	/* access: NULL -> libc, used by the sysfs PWM tests */
};
