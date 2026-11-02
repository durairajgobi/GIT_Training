/*
 * ep_i2c.h - I2C / SMBus master.
 *
 * Transactions are expressed as a list of messages (same model as Linux
 * i2c_msg) so a write-then-read with repeated START is one call. Ports
 * implement a single `xfer` op; a bit-banged master is provided for MCUs
 * without a usable controller and for bus recovery.
 */
#ifndef EP_I2C_H
#define EP_I2C_H

#include "ep_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EP_I2C_M_RD      0x0001u /* read from slave                         */
#define EP_I2C_M_TEN     0x0010u /* 10-bit slave address                    */
#define EP_I2C_M_NOSTART 0x4000u /* continue previous message, no (re)START */

#define EP_I2C_REG_WRITE_MAX 64u /* max payload for ep_i2c_reg_write       */

typedef struct {
    uint16_t addr;
    uint16_t flags;
    uint16_t len;
    uint8_t *buf;
} ep_i2c_msg_t;

typedef struct {
    ep_status_t (*xfer)(void *ctx, ep_i2c_msg_t *msgs, size_t n);
    ep_status_t (*recover)(void *ctx); /* optional: 9-clock bus clear */
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} ep_i2c_bus_ops_t;

typedef struct {
    const ep_i2c_bus_ops_t *ops;
    void *ctx;
    uint8_t retries; /* retries on EP_ERR_ARBITRATION / EP_ERR_IO */
} ep_i2c_bus_t;

typedef struct {
    ep_i2c_bus_t *bus;
    uint16_t addr;
    uint16_t addr_flags;  /* EP_I2C_M_TEN or 0 */
    uint8_t reg_addr_len; /* 1 or 2 byte register/memory address (MSB first) */
    bool pec;             /* SMBus packet error checking */
} ep_i2c_device_t;

ep_status_t ep_i2c_bus_init(ep_i2c_bus_t *bus, const ep_i2c_bus_ops_t *ops, void *ctx);
ep_status_t ep_i2c_transfer(ep_i2c_bus_t *bus, ep_i2c_msg_t *msgs, size_t n);
ep_status_t ep_i2c_recover(ep_i2c_bus_t *bus);

ep_status_t ep_i2c_write(const ep_i2c_device_t *dev, const uint8_t *buf, size_t len);
ep_status_t ep_i2c_read(const ep_i2c_device_t *dev, uint8_t *buf, size_t len);
ep_status_t ep_i2c_write_read(const ep_i2c_device_t *dev, const uint8_t *w, size_t wlen,
                              uint8_t *r, size_t rlen);
ep_status_t ep_i2c_reg_read(const ep_i2c_device_t *dev, uint16_t reg, uint8_t *buf, size_t len);
ep_status_t ep_i2c_reg_write(const ep_i2c_device_t *dev, uint16_t reg, const uint8_t *buf, size_t len);

/* Probe 7-bit addresses 0x08..0x77 with a zero-length write ("quick").
 * found[] is a 128-bit bitmap; returns number of responding devices. */
int ep_i2c_scan(ep_i2c_bus_t *bus, uint8_t found[16]);

/* SMBus 2.0/3.x command protocols (PEC used when dev->pec). */
ep_status_t ep_smbus_read_byte_data(const ep_i2c_device_t *dev, uint8_t cmd, uint8_t *val);
ep_status_t ep_smbus_write_byte_data(const ep_i2c_device_t *dev, uint8_t cmd, uint8_t val);
ep_status_t ep_smbus_read_word_data(const ep_i2c_device_t *dev, uint8_t cmd, uint16_t *val);
ep_status_t ep_smbus_write_word_data(const ep_i2c_device_t *dev, uint8_t cmd, uint16_t val);
ep_status_t ep_smbus_block_read(const ep_i2c_device_t *dev, uint8_t cmd, uint8_t *buf,
                                size_t cap, size_t *out_len);

/* ------------------------------------------------------------------ */
/* Bit-banged master (open-drain GPIO, clock stretching, recovery).    */
/* ------------------------------------------------------------------ */
typedef struct {
    ep_gpio_t gpio;
    const ep_clock_t *clock;
    uint32_t scl, sda;
    uint32_t half_period_us;     /* 5 => ~100 kHz, 1 => ~400 kHz (approx.) */
    uint32_t stretch_timeout_us; /* max time a slave may hold SCL low     */
} ep_i2c_bitbang_t;

extern const ep_i2c_bus_ops_t ep_i2c_bitbang_ops;

void ep_i2c_bitbang_init(ep_i2c_bitbang_t *bb, const ep_gpio_t *gpio, const ep_clock_t *clock,
                         uint32_t scl, uint32_t sda, uint32_t bus_hz);

#ifdef __cplusplus
}
#endif

#endif /* EP_I2C_H */
