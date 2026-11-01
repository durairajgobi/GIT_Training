/* SPDX-License-Identifier: MIT */
/*
 * hal_i2c.c - I2C master via /dev/i2c-N and the I2C_RDWR ioctl.
 */
#include "devhal/hal_i2c.h"
#include "hal_internal.h"

#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOD "i2c"

/* Upper bound for the register-write helpers' stack buffer. */
#define REG_BLOCK_MAX 256

struct hal_i2c {
	int fd;
	unsigned int retries;
	unsigned int retry_delay_us;
	unsigned long funcs;
};

int hal_i2c_open_path(hal_i2c_t **out, const char *path)
{
	hal_i2c_t *h;
	unsigned long funcs = 0;
	int fd, r;

	if (!out || !path)
		return -EINVAL;
	*out = NULL;

	fd = hal_sys_open(path, O_RDWR);
	if (fd < 0) {
		HAL_ERR(MOD, "open %s: %s", path, hal_strerror(fd));
		return fd;
	}
	r = hal_sys_ioctl(fd, I2C_FUNCS, &funcs);
	if (r == 0 && !(funcs & I2C_FUNC_I2C)) {
		HAL_ERR(MOD, "%s: adapter does not support plain I2C_RDWR",
			path);
		hal_sys_close(fd);
		return -EOPNOTSUPP;
	}

	h = calloc(1, sizeof(*h));
	if (!h) {
		hal_sys_close(fd);
		return -ENOMEM;
	}
	h->fd = fd;
	/* If I2C_FUNCS itself failed, assume plain I2C and let transfers tell. */
	h->funcs = r == 0 ? funcs : I2C_FUNC_I2C;
	*out = h;
	return 0;
}

int hal_i2c_open(hal_i2c_t **out, unsigned int bus)
{
	char path[32];

	snprintf(path, sizeof(path), "/dev/i2c-%u", bus);
	return hal_i2c_open_path(out, path);
}

int hal_i2c_close(hal_i2c_t *h)
{
	int r;

	if (!h)
		return -EINVAL;
	r = hal_sys_close(h->fd);
	free(h);
	return r;
}

int hal_i2c_set_retries(hal_i2c_t *h, unsigned int retries,
			unsigned int delay_us)
{
	if (!h)
		return -EINVAL;
	h->retries = retries;
	h->retry_delay_us = delay_us;
	return 0;
}

static bool retryable(int err)
{
	return err == -EAGAIN || err == -EIO || err == -EREMOTEIO ||
	       err == -ENXIO;
}

static int i2c_xfer(hal_i2c_t *h, hal_i2c_msg_t *msgs, size_t count,
		    unsigned int retries)
{
	struct i2c_msg km[HAL_I2C_MAX_MSGS];
	struct i2c_rdwr_ioctl_data data;
	unsigned int attempt;
	size_t i;
	int r;

	if (!h || !msgs || count == 0 || count > HAL_I2C_MAX_MSGS)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		uint16_t addr = msgs[i].addr;

		if (addr > 0x3FF || (msgs[i].len && !msgs[i].buf))
			return -EINVAL;
		km[i].addr = addr;
		km[i].flags = 0;
		if (msgs[i].flags & HAL_I2C_MSG_READ)
			km[i].flags |= I2C_M_RD;
		if (msgs[i].flags & HAL_I2C_MSG_NOSTART) {
			if (!(h->funcs & I2C_FUNC_NOSTART))
				return -EOPNOTSUPP;
			km[i].flags |= I2C_M_NOSTART;
		}
		if (addr > 0x7F) {
			if (!(h->funcs & I2C_FUNC_10BIT_ADDR))
				return -EOPNOTSUPP;
			km[i].flags |= I2C_M_TEN;
		}
		km[i].len = msgs[i].len;
		km[i].buf = msgs[i].buf;
	}
	data.msgs = km;
	data.nmsgs = (__u32)count;

	for (attempt = 0;; attempt++) {
		r = hal_sys_ioctl(h->fd, I2C_RDWR, &data);
		if (r >= 0)
			return 0;
		if (attempt >= retries || !retryable(r))
			break;
		HAL_DBG(MOD, "addr 0x%02x: %s, retry %u/%u", msgs[0].addr,
			hal_strerror(r), attempt + 1, retries);
		if (h->retry_delay_us)
			hal_sleep_us(h->retry_delay_us);
	}
	HAL_DBG(MOD, "addr 0x%02x: transfer failed: %s", msgs[0].addr,
		hal_strerror(r));
	return r;
}

int hal_i2c_transfer(hal_i2c_t *h, hal_i2c_msg_t *msgs, size_t count)
{
	return i2c_xfer(h, msgs, count, h ? h->retries : 0);
}

int hal_i2c_write(hal_i2c_t *h, uint16_t addr, const void *buf, size_t len)
{
	hal_i2c_msg_t m = { addr, 0, (uint16_t)len, (uint8_t *)buf };

	if (len > UINT16_MAX)
		return -EINVAL;
	return hal_i2c_transfer(h, &m, 1);
}

int hal_i2c_read(hal_i2c_t *h, uint16_t addr, void *buf, size_t len)
{
	hal_i2c_msg_t m = { addr, HAL_I2C_MSG_READ, (uint16_t)len, buf };

	if (len == 0 || len > UINT16_MAX)
		return -EINVAL;
	return hal_i2c_transfer(h, &m, 1);
}

int hal_i2c_write_read(hal_i2c_t *h, uint16_t addr, const void *wbuf,
		       size_t wlen, void *rbuf, size_t rlen)
{
	hal_i2c_msg_t m[2] = {
		{ addr, 0, (uint16_t)wlen, (uint8_t *)wbuf },
		{ addr, HAL_I2C_MSG_READ, (uint16_t)rlen, rbuf },
	};

	if (wlen == 0 || rlen == 0 || wlen > UINT16_MAX || rlen > UINT16_MAX)
		return -EINVAL;
	return hal_i2c_transfer(h, m, 2);
}

int hal_i2c_reg_write8(hal_i2c_t *h, uint16_t addr, uint8_t reg, uint8_t val)
{
	uint8_t b[2] = { reg, val };

	return hal_i2c_write(h, addr, b, sizeof(b));
}

int hal_i2c_reg_read8(hal_i2c_t *h, uint16_t addr, uint8_t reg, uint8_t *val)
{
	if (!val)
		return -EINVAL;
	return hal_i2c_write_read(h, addr, &reg, 1, val, 1);
}

int hal_i2c_reg_update8(hal_i2c_t *h, uint16_t addr, uint8_t reg,
			uint8_t mask, uint8_t val)
{
	uint8_t cur;
	int r = hal_i2c_reg_read8(h, addr, reg, &cur);

	if (r < 0)
		return r;
	val = (uint8_t)((cur & ~mask) | (val & mask));
	if (val == cur)
		return 0;
	return hal_i2c_reg_write8(h, addr, reg, val);
}

int hal_i2c_reg_write16be(hal_i2c_t *h, uint16_t addr, uint8_t reg,
			  uint16_t val)
{
	uint8_t b[3] = { reg, (uint8_t)(val >> 8), (uint8_t)val };

	return hal_i2c_write(h, addr, b, sizeof(b));
}

int hal_i2c_reg_read16be(hal_i2c_t *h, uint16_t addr, uint8_t reg,
			 uint16_t *val)
{
	uint8_t b[2];
	int r;

	if (!val)
		return -EINVAL;
	r = hal_i2c_write_read(h, addr, &reg, 1, b, 2);
	if (r == 0)
		*val = (uint16_t)((b[0] << 8) | b[1]);
	return r;
}

int hal_i2c_reg_write_block(hal_i2c_t *h, uint16_t addr, uint8_t reg,
			    const void *buf, size_t len)
{
	uint8_t b[REG_BLOCK_MAX + 1];

	if ((len && !buf) || len > REG_BLOCK_MAX)
		return -EINVAL;
	b[0] = reg;
	if (len)
		memcpy(b + 1, buf, len);
	return hal_i2c_write(h, addr, b, len + 1);
}

int hal_i2c_reg_read_block(hal_i2c_t *h, uint16_t addr, uint8_t reg,
			   void *buf, size_t len)
{
	return hal_i2c_write_read(h, addr, &reg, 1, buf, len);
}

int hal_i2c_probe(hal_i2c_t *h, uint16_t addr)
{
	uint8_t b;
	hal_i2c_msg_t m = { addr, HAL_I2C_MSG_READ, 1, &b };

	/* A probe must answer "is anyone there?" at once: no retries. */
	return i2c_xfer(h, &m, 1, 0);
}

int hal_i2c_write_cb(void *user, uint8_t addr7, const uint8_t *buf, size_t len)
{
	return hal_i2c_write(user, addr7, buf, len);
}
