#include "ep/ep_spi.h"

#include <string.h>

ep_status_t ep_spi_bus_init(ep_spi_bus_t *bus, const ep_spi_bus_ops_t *ops, void *ctx,
                            const ep_clock_t *clock)
{
    if (!bus || !ops || (!ops->transfer && !ops->transfer_segments))
        return EP_ERR_PARAM;
    memset(bus, 0, sizeof(*bus));
    bus->ops = ops;
    bus->ctx = ctx;
    bus->clock = clock;
    return EP_OK;
}

ep_status_t ep_spi_device_init(ep_spi_device_t *dev, ep_spi_bus_t *bus, uint8_t cs,
                               const ep_spi_config_t *cfg)
{
    if (!dev || !bus || !cfg || cfg->mode > EP_SPI_MODE3 || cfg->max_hz == 0 ||
        cfg->bits_per_word == 0 || cfg->bits_per_word > 32)
        return EP_ERR_PARAM;
    dev->bus = bus;
    dev->cs = cs;
    dev->cfg = *cfg;
    return EP_OK;
}

static bool cfg_equal(const ep_spi_config_t *a, const ep_spi_config_t *b)
{
    return a->max_hz == b->max_hz && a->mode == b->mode &&
           a->bits_per_word == b->bits_per_word && a->lsb_first == b->lsb_first &&
           a->cs_active_high == b->cs_active_high;
}

static ep_status_t apply_config(ep_spi_device_t *dev)
{
    ep_spi_bus_t *bus = dev->bus;
    if (bus->active_valid && cfg_equal(&bus->active, &dev->cfg))
        return EP_OK;
    if (bus->ops->configure) {
        ep_status_t st = bus->ops->configure(bus->ctx, &dev->cfg);
        if (st != EP_OK) {
            bus->active_valid = false;
            return st;
        }
    }
    bus->active = dev->cfg;
    bus->active_valid = true;
    return EP_OK;
}

ep_status_t ep_spi_transfer_segments(ep_spi_device_t *dev, const ep_spi_segment_t *segs, size_t n)
{
    if (!dev || !dev->bus || (!segs && n))
        return EP_ERR_PARAM;
    ep_spi_bus_t *bus = dev->bus;
    const ep_spi_bus_ops_t *ops = bus->ops;
    ep_status_t st;

    if (ops->lock)
        ops->lock(bus->ctx);

    st = apply_config(dev);
    if (st != EP_OK)
        goto out;

    if (ops->transfer_segments) {
        st = ops->transfer_segments(bus->ctx, dev->cs, segs, n);
        goto out;
    }

    bool hi = dev->cfg.cs_active_high;
    bool selected = false;
    for (size_t i = 0; i < n; i++) {
        if (!selected && ops->chip_select) {
            ops->chip_select(bus->ctx, dev->cs, true, hi);
            selected = true;
        }
        st = ops->transfer(bus->ctx, segs[i].tx, segs[i].rx, segs[i].len);
        if (st != EP_OK)
            break;
        ep_clock_delay_us(bus->clock, segs[i].delay_us);
        if (segs[i].cs_change && i + 1 < n && ops->chip_select) {
            ops->chip_select(bus->ctx, dev->cs, false, hi);
            selected = false;
        }
    }
    if (ops->chip_select)
        ops->chip_select(bus->ctx, dev->cs, false, hi);

out:
    if (ops->unlock)
        ops->unlock(bus->ctx);
    return st;
}

ep_status_t ep_spi_transfer(ep_spi_device_t *dev, const uint8_t *tx, uint8_t *rx, size_t len)
{
    ep_spi_segment_t seg = {.tx = tx, .rx = rx, .len = len};
    return ep_spi_transfer_segments(dev, &seg, 1);
}

ep_status_t ep_spi_write(ep_spi_device_t *dev, const uint8_t *tx, size_t len)
{
    return ep_spi_transfer(dev, tx, NULL, len);
}

ep_status_t ep_spi_write_then_read(ep_spi_device_t *dev, const uint8_t *tx, size_t txlen,
                                   uint8_t *rx, size_t rxlen)
{
    ep_spi_segment_t segs[2] = {
        {.tx = tx, .len = txlen},
        {.rx = rx, .len = rxlen},
    };
    return ep_spi_transfer_segments(dev, segs, 2);
}

ep_status_t ep_spi_reg_read(ep_spi_device_t *dev, uint8_t reg, uint8_t read_flag,
                            uint8_t *buf, size_t len)
{
    uint8_t cmd = (uint8_t)(reg | read_flag);
    return ep_spi_write_then_read(dev, &cmd, 1, buf, len);
}

ep_status_t ep_spi_reg_write(ep_spi_device_t *dev, uint8_t reg, const uint8_t *buf, size_t len)
{
    ep_spi_segment_t segs[2] = {
        {.tx = &reg, .len = 1},
        {.tx = buf, .len = len},
    };
    return ep_spi_transfer_segments(dev, segs, 2);
}

/* ------------------------------------------------------------------ */
/* Bit-bang                                                            */
/* ------------------------------------------------------------------ */

void ep_spi_bitbang_init(ep_spi_bitbang_t *bb, const ep_gpio_t *gpio, const ep_clock_t *clock,
                         uint32_t sck, uint32_t mosi, uint32_t miso,
                         const uint32_t *cs_pins, size_t n_cs)
{
    memset(bb, 0, sizeof(*bb));
    bb->gpio = *gpio;
    bb->clock = clock;
    bb->sck = sck;
    bb->mosi = mosi;
    bb->miso = miso;
    for (size_t i = 0; i < EP_SPI_BB_MAX_CS; i++)
        bb->cs_pins[i] = (cs_pins && i < n_cs) ? cs_pins[i] : EP_PIN_NONE;
    for (size_t i = 0; i < EP_SPI_BB_MAX_CS && i < n_cs; i++)
        ep_gpio_write(&bb->gpio, bb->cs_pins[i], true); /* deselect (active-low default) */
}

static ep_status_t bb_configure(void *ctx, const ep_spi_config_t *cfg)
{
    ep_spi_bitbang_t *bb = ctx;
    if (cfg->bits_per_word != 8)
        return EP_ERR_UNSUPPORTED;
    bb->cfg = *cfg;
    uint32_t hp = 500000u / (cfg->max_hz ? cfg->max_hz : 1);
    bb->half_period_us = hp; /* 0 => as fast as GPIO toggles */
    ep_gpio_write(&bb->gpio, bb->sck, (cfg->mode & 2) != 0);
    return EP_OK;
}

static void bb_chip_select(void *ctx, uint8_t cs, bool assert_cs, bool active_high)
{
    ep_spi_bitbang_t *bb = ctx;
    if (cs >= EP_SPI_BB_MAX_CS)
        return;
    ep_gpio_write(&bb->gpio, bb->cs_pins[cs], assert_cs == active_high);
}

static uint8_t bb_xfer_byte(ep_spi_bitbang_t *bb, uint8_t out)
{
    const bool cpol = (bb->cfg.mode & 2) != 0;
    const bool cpha = (bb->cfg.mode & 1) != 0;
    uint8_t in = 0;

    for (int i = 0; i < 8; i++) {
        int bit = bb->cfg.lsb_first ? i : 7 - i;
        bool obit = (out >> bit) & 1;
        bool ibit;
        if (!cpha) {
            /* data valid before leading edge; sample on leading edge */
            ep_gpio_write(&bb->gpio, bb->mosi, obit);
            ep_clock_delay_us(bb->clock, bb->half_period_us);
            ep_gpio_write(&bb->gpio, bb->sck, !cpol);
            ibit = ep_gpio_read(&bb->gpio, bb->miso);
            ep_clock_delay_us(bb->clock, bb->half_period_us);
            ep_gpio_write(&bb->gpio, bb->sck, cpol);
        } else {
            /* shift on leading edge; sample on trailing edge */
            ep_gpio_write(&bb->gpio, bb->sck, !cpol);
            ep_gpio_write(&bb->gpio, bb->mosi, obit);
            ep_clock_delay_us(bb->clock, bb->half_period_us);
            ep_gpio_write(&bb->gpio, bb->sck, cpol);
            ibit = ep_gpio_read(&bb->gpio, bb->miso);
            ep_clock_delay_us(bb->clock, bb->half_period_us);
        }
        if (ibit)
            in |= (uint8_t)(1u << bit);
    }
    return in;
}

static ep_status_t bb_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    ep_spi_bitbang_t *bb = ctx;
    for (size_t i = 0; i < len; i++) {
        uint8_t in = bb_xfer_byte(bb, tx ? tx[i] : 0x00);
        if (rx)
            rx[i] = in;
    }
    return EP_OK;
}

const ep_spi_bus_ops_t ep_spi_bitbang_ops = {
    .configure = bb_configure,
    .transfer = bb_transfer,
    .chip_select = bb_chip_select,
};
