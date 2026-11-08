/* SPDX-License-Identifier: MIT */
/*
 * hal_i2c.h - I2C master access via /dev/i2c-N.
 *
 * Every transfer goes through the I2C_RDWR ioctl with the slave address in
 * the message, so a single bus handle can talk to many devices from many
 * threads without the I2C_SLAVE "current address" race.
 *
 * Addresses above 0x7F are sent as 10-bit addresses.
 */
#ifndef DEVHAL_HAL_I2C_H
#define DEVHAL_HAL_I2C_H

#include "hal_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_i2c hal_i2c_t;

#define HAL_I2C_MSG_READ   0x0001u  /* read into buf (else write from buf) */
#define HAL_I2C_MSG_NOSTART 0x4000u /* no repeated start before this msg */

typedef struct {
	uint16_t addr;
	uint16_t flags;     /* HAL_I2C_MSG_* */
	uint16_t len;
	uint8_t *buf;
} hal_i2c_msg_t;

/* Kernel limit on messages in one combined transaction. */
#define HAL_I2C_MAX_MSGS 42

int hal_i2c_open(hal_i2c_t **out, unsigned int bus);       /* /dev/i2c-<bus> */
int hal_i2c_open_path(hal_i2c_t **out, const char *path);
int hal_i2c_close(hal_i2c_t *i2c);

/*
 * Retry a transfer this many extra times when it fails with -EAGAIN, -EIO,
 * -EREMOTEIO or -ENXIO (e.g. an EEPROM busy with its write cycle).
 * @delay_us is the pause between attempts. Default: no retries.
 */
int hal_i2c_set_retries(hal_i2c_t *i2c, unsigned int retries,
			unsigned int delay_us);

/* Combined transaction with repeated starts between messages. */
int hal_i2c_transfer(hal_i2c_t *i2c, hal_i2c_msg_t *msgs, size_t count);

int hal_i2c_write(hal_i2c_t *i2c, uint16_t addr, const void *buf, size_t len);
int hal_i2c_read(hal_i2c_t *i2c, uint16_t addr, void *buf, size_t len);
int hal_i2c_write_read(hal_i2c_t *i2c, uint16_t addr,
		       const void *wbuf, size_t wlen, void *rbuf, size_t rlen);

/* Register helpers for the common "8-bit register pointer" device layout. */
int hal_i2c_reg_write8(hal_i2c_t *i2c, uint16_t addr, uint8_t reg, uint8_t val);
int hal_i2c_reg_read8(hal_i2c_t *i2c, uint16_t addr, uint8_t reg, uint8_t *val);
int hal_i2c_reg_update8(hal_i2c_t *i2c, uint16_t addr, uint8_t reg,
			uint8_t mask, uint8_t val);
/* 16-bit register values, big-endian on the wire (most sensors). */
int hal_i2c_reg_write16be(hal_i2c_t *i2c, uint16_t addr, uint8_t reg,
			  uint16_t val);
int hal_i2c_reg_read16be(hal_i2c_t *i2c, uint16_t addr, uint8_t reg,
			 uint16_t *val);
int hal_i2c_reg_write_block(hal_i2c_t *i2c, uint16_t addr, uint8_t reg,
			    const void *buf, size_t len);
int hal_i2c_reg_read_block(hal_i2c_t *i2c, uint16_t addr, uint8_t reg,
			   void *buf, size_t len);

/* Returns 0 if a device ACKs a 1-byte read at @addr, -ENXIO/-EREMOTEIO if not. */
int hal_i2c_probe(hal_i2c_t *i2c, uint16_t addr);

/*
 * Adapter for portable drivers that take a write callback, e.g.
 * ltc3562_i2c_write_fn: pass hal_i2c_write_cb with the handle as @user.
 */
int hal_i2c_write_cb(void *user, uint8_t addr7, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* DEVHAL_HAL_I2C_H */
