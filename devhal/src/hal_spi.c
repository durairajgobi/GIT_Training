/* SPDX-License-Identifier: MIT */
/*
 * hal_spi.c - SPI master via spidev.
 */
#include "devhal/hal_spi.h"
#include "hal_internal.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOD "spi"

struct hal_spi {
	int fd;
	hal_spi_config_t cfg;
	pthread_mutex_t lock;   /* guards cfg against concurrent transfers */
};

static int spi_apply(int fd, const hal_spi_config_t *cfg)
{
	uint8_t mode, bits, lsb;
	uint32_t speed;
	int r;

	if (cfg->mode > 3 || cfg->max_speed_hz == 0 || cfg->bits_per_word > 32)
		return -EINVAL;
	mode = cfg->mode;
	if (cfg->cs_high)
		mode |= SPI_CS_HIGH;
	if (cfg->lsb_first)
		mode |= SPI_LSB_FIRST;
	bits = cfg->bits_per_word ? cfg->bits_per_word : 8;
	lsb = cfg->lsb_first ? 1 : 0;
	speed = cfg->max_speed_hz;

	if ((r = hal_sys_ioctl(fd, SPI_IOC_WR_MODE, &mode)) < 0 ||
	    (r = hal_sys_ioctl(fd, SPI_IOC_WR_LSB_FIRST, &lsb)) < 0 ||
	    (r = hal_sys_ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits)) < 0 ||
	    (r = hal_sys_ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed)) < 0) {
		HAL_ERR(MOD, "configure mode %u bits %u %u Hz: %s", cfg->mode,
			bits, speed, hal_strerror(r));
		return r;
	}
	return 0;
}

int hal_spi_open_path(hal_spi_t **out, const char *path,
		      const hal_spi_config_t *cfg)
{
	hal_spi_config_t def = HAL_SPI_CONFIG_INIT;
	hal_spi_t *s;
	int fd, r;

	if (!out || !path)
		return -EINVAL;
	*out = NULL;
	if (!cfg)
		cfg = &def;

	fd = hal_sys_open(path, O_RDWR);
	if (fd < 0) {
		HAL_ERR(MOD, "open %s: %s", path, hal_strerror(fd));
		return fd;
	}
	r = spi_apply(fd, cfg);
	if (r < 0) {
		hal_sys_close(fd);
		return r;
	}
	s = calloc(1, sizeof(*s));
	if (!s) {
		hal_sys_close(fd);
		return -ENOMEM;
	}
	s->fd = fd;
	s->cfg = *cfg;
	if (!s->cfg.bits_per_word)
		s->cfg.bits_per_word = 8;
	pthread_mutex_init(&s->lock, NULL);
	*out = s;
	return 0;
}

int hal_spi_open(hal_spi_t **out, unsigned int bus, unsigned int cs,
		 const hal_spi_config_t *cfg)
{
	char path[48];

	snprintf(path, sizeof(path), "/dev/spidev%u.%u", bus, cs);
	return hal_spi_open_path(out, path, cfg);
}

int hal_spi_close(hal_spi_t *s)
{
	int r;

	if (!s)
		return -EINVAL;
	r = hal_sys_close(s->fd);
	pthread_mutex_destroy(&s->lock);
	free(s);
	return r;
}

int hal_spi_configure(hal_spi_t *s, const hal_spi_config_t *cfg)
{
	int r;

	if (!s || !cfg)
		return -EINVAL;
	pthread_mutex_lock(&s->lock);
	r = spi_apply(s->fd, cfg);
	if (r == 0) {
		s->cfg = *cfg;
		if (!s->cfg.bits_per_word)
			s->cfg.bits_per_word = 8;
	}
	pthread_mutex_unlock(&s->lock);
	return r;
}

int hal_spi_get_config(hal_spi_t *s, hal_spi_config_t *cfg)
{
	if (!s || !cfg)
		return -EINVAL;
	pthread_mutex_lock(&s->lock);
	*cfg = s->cfg;
	pthread_mutex_unlock(&s->lock);
	return 0;
}

int hal_spi_transfer_multi(hal_spi_t *s, const hal_spi_xfer_t *x, size_t n)
{
	struct spi_ioc_transfer tr[HAL_SPI_MAX_XFERS];
	size_t i;
	int r;

	if (!s || !x || n == 0 || n > HAL_SPI_MAX_XFERS)
		return -EINVAL;

	memset(tr, 0, n * sizeof(tr[0]));
	pthread_mutex_lock(&s->lock);
	for (i = 0; i < n; i++) {
		if (x[i].len == 0 || (!x[i].tx && !x[i].rx)) {
			pthread_mutex_unlock(&s->lock);
			return -EINVAL;
		}
		tr[i].tx_buf = (uintptr_t)x[i].tx;
		tr[i].rx_buf = (uintptr_t)x[i].rx;
		tr[i].len = x[i].len;
		tr[i].speed_hz = x[i].speed_hz ? x[i].speed_hz
					       : s->cfg.max_speed_hz;
		tr[i].bits_per_word = s->cfg.bits_per_word;
		tr[i].delay_usecs = x[i].delay_us;
		tr[i].cs_change = x[i].cs_change;
	}
	r = hal_sys_ioctl(s->fd, SPI_IOC_MESSAGE(n), tr);
	pthread_mutex_unlock(&s->lock);
	if (r < 0) {
		HAL_DBG(MOD, "transfer of %zu segment(s): %s", n,
			hal_strerror(r));
		return r;
	}
	return 0;
}

int hal_spi_transfer(hal_spi_t *s, const void *tx, void *rx, size_t len)
{
	hal_spi_xfer_t x = { tx, rx, (uint32_t)len, 0, 0, false };

	if (len > UINT32_MAX)
		return -EINVAL;
	return hal_spi_transfer_multi(s, &x, 1);
}

int hal_spi_write(hal_spi_t *s, const void *tx, size_t len)
{
	if (!tx)
		return -EINVAL;
	return hal_spi_transfer(s, tx, NULL, len);
}

int hal_spi_read(hal_spi_t *s, void *rx, size_t len)
{
	if (!rx)
		return -EINVAL;
	return hal_spi_transfer(s, NULL, rx, len);
}

int hal_spi_write_then_read(hal_spi_t *s, const void *tx, size_t txlen,
			    void *rx, size_t rxlen)
{
	hal_spi_xfer_t x[2] = {
		{ tx, NULL, (uint32_t)txlen, 0, 0, false },
		{ NULL, rx, (uint32_t)rxlen, 0, 0, false },
	};

	if (!tx || !rx || txlen > UINT32_MAX || rxlen > UINT32_MAX)
		return -EINVAL;
	return hal_spi_transfer_multi(s, x, 2);
}
