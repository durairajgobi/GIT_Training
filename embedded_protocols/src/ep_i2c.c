#include "ep/ep_i2c.h"
#include "ep/ep_crc.h"

#include <string.h>

ep_status_t ep_i2c_bus_init(ep_i2c_bus_t *bus, const ep_i2c_bus_ops_t *ops, void *ctx)
{
    if (!bus || !ops || !ops->xfer)
        return EP_ERR_PARAM;
    bus->ops = ops;
    bus->ctx = ctx;
    bus->retries = 2;
    return EP_OK;
}

ep_status_t ep_i2c_transfer(ep_i2c_bus_t *bus, ep_i2c_msg_t *msgs, size_t n)
{
    if (!bus || !msgs || n == 0)
        return EP_ERR_PARAM;
    ep_status_t st = EP_ERR_IO;
    if (bus->ops->lock)
        bus->ops->lock(bus->ctx);
    for (unsigned attempt = 0; attempt <= bus->retries; attempt++) {
        st = bus->ops->xfer(bus->ctx, msgs, n);
        if (st != EP_ERR_ARBITRATION && st != EP_ERR_IO)
            break;
        /* stuck bus after a glitch: try to clear it before retrying */
        if (st == EP_ERR_IO && bus->ops->recover)
            bus->ops->recover(bus->ctx);
    }
    if (bus->ops->unlock)
        bus->ops->unlock(bus->ctx);
    return st;
}

ep_status_t ep_i2c_recover(ep_i2c_bus_t *bus)
{
    if (!bus || !bus->ops->recover)
        return EP_ERR_UNSUPPORTED;
    return bus->ops->recover(bus->ctx);
}

ep_status_t ep_i2c_write(const ep_i2c_device_t *dev, const uint8_t *buf, size_t len)
{
    if (len > 0xFFFF)
        return EP_ERR_PARAM;
    ep_i2c_msg_t m = {dev->addr, dev->addr_flags, (uint16_t)len, (uint8_t *)buf};
    return ep_i2c_transfer(dev->bus, &m, 1);
}

ep_status_t ep_i2c_read(const ep_i2c_device_t *dev, uint8_t *buf, size_t len)
{
    if (len > 0xFFFF)
        return EP_ERR_PARAM;
    ep_i2c_msg_t m = {dev->addr, (uint16_t)(dev->addr_flags | EP_I2C_M_RD), (uint16_t)len, buf};
    return ep_i2c_transfer(dev->bus, &m, 1);
}

ep_status_t ep_i2c_write_read(const ep_i2c_device_t *dev, const uint8_t *w, size_t wlen,
                              uint8_t *r, size_t rlen)
{
    if (wlen > 0xFFFF || rlen > 0xFFFF)
        return EP_ERR_PARAM;
    ep_i2c_msg_t m[2] = {
        {dev->addr, dev->addr_flags, (uint16_t)wlen, (uint8_t *)w},
        {dev->addr, (uint16_t)(dev->addr_flags | EP_I2C_M_RD), (uint16_t)rlen, r},
    };
    return ep_i2c_transfer(dev->bus, m, 2);
}

static size_t put_reg(const ep_i2c_device_t *dev, uint8_t *out, uint16_t reg)
{
    if (dev->reg_addr_len == 2) {
        ep_put_be16(out, reg);
        return 2;
    }
    out[0] = (uint8_t)reg;
    return 1;
}

ep_status_t ep_i2c_reg_read(const ep_i2c_device_t *dev, uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t a[2];
    size_t alen = put_reg(dev, a, reg);
    return ep_i2c_write_read(dev, a, alen, buf, len);
}

ep_status_t ep_i2c_reg_write(const ep_i2c_device_t *dev, uint16_t reg, const uint8_t *buf, size_t len)
{
    uint8_t tmp[2 + EP_I2C_REG_WRITE_MAX];
    if (len > EP_I2C_REG_WRITE_MAX || (!buf && len))
        return EP_ERR_PARAM;
    size_t alen = put_reg(dev, tmp, reg);
    if (len)
        memcpy(tmp + alen, buf, len);
    return ep_i2c_write(dev, tmp, alen + len);
}

int ep_i2c_scan(ep_i2c_bus_t *bus, uint8_t found[16])
{
    int count = 0;
    memset(found, 0, 16);
    for (uint16_t a = 0x08; a <= 0x77; a++) {
        ep_i2c_msg_t m = {a, 0, 0, NULL};
        if (bus->ops->xfer(bus->ctx, &m, 1) == EP_OK) {
            found[a >> 3] |= (uint8_t)(1u << (a & 7));
            count++;
        }
    }
    return count;
}

/* ---------------------------- SMBus -------------------------------- */

static uint8_t pec_addr(uint16_t addr, bool rd)
{
    return (uint8_t)((addr << 1) | (rd ? 1 : 0));
}

/* write: [cmd][data...][pec?] */
static ep_status_t smbus_write(const ep_i2c_device_t *dev, uint8_t cmd, const uint8_t *data, size_t n)
{
    uint8_t buf[2 + 32 + 1];
    if (n > 32)
        return EP_ERR_PARAM;
    buf[0] = cmd;
    memcpy(buf + 1, data, n);
    size_t len = 1 + n;
    if (dev->pec) {
        uint8_t a = pec_addr(dev->addr, false);
        uint8_t crc = ep_crc8_smbus(EP_CRC8_SMBUS_INIT, &a, 1);
        buf[len] = ep_crc8_smbus(crc, buf, len);
        len++;
    }
    return ep_i2c_write(dev, buf, len);
}

/* read: [cmd] Sr [data...][pec?]; data is n bytes */
static ep_status_t smbus_read(const ep_i2c_device_t *dev, uint8_t cmd, uint8_t *data, size_t n)
{
    uint8_t buf[33 + 1];
    size_t rlen = n + (dev->pec ? 1 : 0);
    if (rlen > sizeof(buf))
        return EP_ERR_PARAM;
    ep_status_t st = ep_i2c_write_read(dev, &cmd, 1, buf, rlen);
    if (st != EP_OK)
        return st;
    if (dev->pec) {
        uint8_t hdr[3] = {pec_addr(dev->addr, false), cmd, pec_addr(dev->addr, true)};
        uint8_t crc = ep_crc8_smbus(EP_CRC8_SMBUS_INIT, hdr, 3);
        crc = ep_crc8_smbus(crc, buf, n);
        if (crc != buf[n])
            return EP_ERR_CRC;
    }
    memcpy(data, buf, n);
    return EP_OK;
}

ep_status_t ep_smbus_read_byte_data(const ep_i2c_device_t *dev, uint8_t cmd, uint8_t *val)
{
    return smbus_read(dev, cmd, val, 1);
}

ep_status_t ep_smbus_write_byte_data(const ep_i2c_device_t *dev, uint8_t cmd, uint8_t val)
{
    return smbus_write(dev, cmd, &val, 1);
}

ep_status_t ep_smbus_read_word_data(const ep_i2c_device_t *dev, uint8_t cmd, uint16_t *val)
{
    uint8_t b[2];
    ep_status_t st = smbus_read(dev, cmd, b, 2);
    if (st == EP_OK)
        *val = ep_get_le16(b); /* SMBus words are little-endian */
    return st;
}

ep_status_t ep_smbus_write_word_data(const ep_i2c_device_t *dev, uint8_t cmd, uint16_t val)
{
    uint8_t b[2];
    ep_put_le16(b, val);
    return smbus_write(dev, cmd, b, 2);
}

ep_status_t ep_smbus_block_read(const ep_i2c_device_t *dev, uint8_t cmd, uint8_t *buf,
                                size_t cap, size_t *out_len)
{
    /* Generic controllers cannot change read length mid-transfer, so read
     * the maximum (count byte + 32 + PEC) and validate the count. */
    uint8_t raw[1 + 32 + 1];
    size_t rlen = 1 + 32 + (dev->pec ? 1 : 0);
    ep_status_t st = ep_i2c_write_read(dev, &cmd, 1, raw, rlen);
    if (st != EP_OK)
        return st;
    uint8_t count = raw[0];
    if (count == 0 || count > 32)
        return EP_ERR_FRAME;
    if (count > cap)
        return EP_ERR_OVERFLOW;
    if (dev->pec) {
        uint8_t hdr[3] = {pec_addr(dev->addr, false), cmd, pec_addr(dev->addr, true)};
        uint8_t crc = ep_crc8_smbus(EP_CRC8_SMBUS_INIT, hdr, 3);
        crc = ep_crc8_smbus(crc, raw, 1u + count);
        if (crc != raw[1 + count])
            return EP_ERR_CRC;
    }
    memcpy(buf, raw + 1, count);
    *out_len = count;
    return EP_OK;
}

/* --------------------------- Bit-bang ------------------------------ */

void ep_i2c_bitbang_init(ep_i2c_bitbang_t *bb, const ep_gpio_t *gpio, const ep_clock_t *clock,
                         uint32_t scl, uint32_t sda, uint32_t bus_hz)
{
    bb->gpio = *gpio;
    bb->clock = clock;
    bb->scl = scl;
    bb->sda = sda;
    bb->half_period_us = bus_hz ? 500000u / bus_hz : 0;
    bb->stretch_timeout_us = 25000; /* SMBus tTIMEOUT */
    ep_gpio_write(&bb->gpio, sda, true);
    ep_gpio_write(&bb->gpio, scl, true);
}

static void bb_delay(ep_i2c_bitbang_t *bb)
{
    ep_clock_delay_us(bb->clock, bb->half_period_us);
}

static void sda_set(ep_i2c_bitbang_t *bb, bool v) { ep_gpio_write(&bb->gpio, bb->sda, v); }
static bool sda_get(ep_i2c_bitbang_t *bb) { return ep_gpio_read(&bb->gpio, bb->sda); }
static void scl_low(ep_i2c_bitbang_t *bb) { ep_gpio_write(&bb->gpio, bb->scl, false); }

/* Release SCL and wait for slaves that stretch the clock. */
static ep_status_t scl_high(ep_i2c_bitbang_t *bb)
{
    ep_gpio_write(&bb->gpio, bb->scl, true);
    uint32_t waited = 0;
    while (!ep_gpio_read(&bb->gpio, bb->scl)) {
        if (waited >= bb->stretch_timeout_us)
            return EP_ERR_TIMEOUT;
        ep_clock_delay_us(bb->clock, 1);
        waited++;
        if (!bb->clock || !bb->clock->delay_us)
            return EP_ERR_TIMEOUT; /* cannot wait without a delay source */
    }
    return EP_OK;
}

static ep_status_t bb_start(ep_i2c_bitbang_t *bb)
{
    /* works for START and repeated START (SCL may currently be low) */
    sda_set(bb, true);
    bb_delay(bb);
    ep_status_t st = scl_high(bb);
    if (st != EP_OK)
        return st;
    if (!sda_get(bb))
        return EP_ERR_ARBITRATION; /* someone else holds SDA */
    bb_delay(bb);
    sda_set(bb, false);
    bb_delay(bb);
    scl_low(bb);
    return EP_OK;
}

static ep_status_t bb_stop(ep_i2c_bitbang_t *bb)
{
    sda_set(bb, false);
    bb_delay(bb);
    ep_status_t st = scl_high(bb);
    bb_delay(bb);
    sda_set(bb, true);
    bb_delay(bb);
    return st;
}

/* returns EP_OK on ACK, EP_ERR_NACK on NACK */
static ep_status_t bb_write_byte(ep_i2c_bitbang_t *bb, uint8_t byte)
{
    ep_status_t st;
    for (int i = 7; i >= 0; i--) {
        bool bit = (byte >> i) & 1;
        sda_set(bb, bit);
        bb_delay(bb);
        if ((st = scl_high(bb)) != EP_OK)
            return st;
        if (bit && !sda_get(bb)) {
            sda_set(bb, true);
            return EP_ERR_ARBITRATION;
        }
        bb_delay(bb);
        scl_low(bb);
    }
    sda_set(bb, true); /* release for ACK */
    bb_delay(bb);
    if ((st = scl_high(bb)) != EP_OK)
        return st;
    bool nack = sda_get(bb);
    bb_delay(bb);
    scl_low(bb);
    return nack ? EP_ERR_NACK : EP_OK;
}

static ep_status_t bb_read_byte(ep_i2c_bitbang_t *bb, uint8_t *out, bool ack)
{
    ep_status_t st;
    uint8_t v = 0;
    sda_set(bb, true);
    for (int i = 0; i < 8; i++) {
        bb_delay(bb);
        if ((st = scl_high(bb)) != EP_OK)
            return st;
        v = (uint8_t)((v << 1) | (sda_get(bb) ? 1 : 0));
        bb_delay(bb);
        scl_low(bb);
    }
    sda_set(bb, !ack);
    bb_delay(bb);
    if ((st = scl_high(bb)) != EP_OK)
        return st;
    bb_delay(bb);
    scl_low(bb);
    sda_set(bb, true);
    *out = v;
    return EP_OK;
}

static ep_status_t bb_xfer(void *ctx, ep_i2c_msg_t *msgs, size_t n)
{
    ep_i2c_bitbang_t *bb = ctx;
    ep_status_t st = EP_OK;

    for (size_t i = 0; i < n; i++) {
        ep_i2c_msg_t *m = &msgs[i];
        bool rd = (m->flags & EP_I2C_M_RD) != 0;
        if (m->flags & EP_I2C_M_TEN) {
            st = EP_ERR_UNSUPPORTED;
            break;
        }
        if (!(m->flags & EP_I2C_M_NOSTART) || i == 0) {
            if ((st = bb_start(bb)) != EP_OK)
                break;
            if ((st = bb_write_byte(bb, (uint8_t)((m->addr << 1) | (rd ? 1 : 0)))) != EP_OK)
                break;
        }
        for (uint16_t k = 0; k < m->len && st == EP_OK; k++) {
            if (rd)
                st = bb_read_byte(bb, &m->buf[k], k + 1 < m->len); /* NACK last byte */
            else
                st = bb_write_byte(bb, m->buf[k]);
        }
        if (st != EP_OK)
            break;
    }
    /* After losing arbitration the bus belongs to another master: no STOP. */
    if (st != EP_ERR_ARBITRATION)
        bb_stop(bb);
    return st;
}

static ep_status_t bb_recover(void *ctx)
{
    ep_i2c_bitbang_t *bb = ctx;
    /* A slave stuck mid-byte holds SDA low; clock it out (max 9 pulses). */
    sda_set(bb, true);
    for (int i = 0; i < 9 && !sda_get(bb); i++) {
        scl_low(bb);
        bb_delay(bb);
        if (scl_high(bb) != EP_OK)
            return EP_ERR_TIMEOUT;
        bb_delay(bb);
    }
    if (!sda_get(bb))
        return EP_ERR_IO;
    scl_low(bb);
    bb_delay(bb);
    return bb_stop(bb);
}

const ep_i2c_bus_ops_t ep_i2c_bitbang_ops = {
    .xfer = bb_xfer,
    .recover = bb_recover,
};
