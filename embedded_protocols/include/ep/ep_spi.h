/*
 * ep_spi.h - SPI master: bus/device model, register helpers, bit-bang driver.
 *
 * A bus (one SPI peripheral) is shared by several devices, each with its
 * own chip-select and its own clock/mode settings. The core reconfigures
 * the peripheral only when the device being addressed changes settings.
 *
 * Porting: implement ep_spi_bus_ops_t for your controller (register level,
 * DMA, Linux spidev, ...). `transfer` must handle tx==NULL (send 0x00)
 * and rx==NULL (discard). If the controller drives CS itself (Linux
 * spidev, QSPI) implement `transfer_segments` and leave `chip_select` NULL.
 */
#ifndef EP_SPI_H
#define EP_SPI_H

#include "ep_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EP_SPI_MODE0 = 0, /* CPOL=0 CPHA=0: idle low,  sample rising  */
    EP_SPI_MODE1 = 1, /* CPOL=0 CPHA=1: idle low,  sample falling */
    EP_SPI_MODE2 = 2, /* CPOL=1 CPHA=0: idle high, sample falling */
    EP_SPI_MODE3 = 3  /* CPOL=1 CPHA=1: idle high, sample rising  */
} ep_spi_mode_t;

typedef struct {
    uint32_t max_hz;
    ep_spi_mode_t mode;
    uint8_t bits_per_word; /* 8 for almost every device */
    bool lsb_first;
    bool cs_active_high;
} ep_spi_config_t;

/* One piece of a transaction; CS stays asserted across segments unless
 * cs_change is set (then CS toggles after this segment). */
typedef struct {
    const uint8_t *tx;
    uint8_t *rx;
    size_t len;
    bool cs_change;
    uint16_t delay_us; /* delay after this segment */
} ep_spi_segment_t;

typedef struct {
    ep_status_t (*configure)(void *ctx, const ep_spi_config_t *cfg);
    ep_status_t (*transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
    void (*chip_select)(void *ctx, uint8_t cs, bool assert_cs, bool active_high);
    /* optional: whole-transaction offload (controller-managed CS) */
    ep_status_t (*transfer_segments)(void *ctx, uint8_t cs, const ep_spi_segment_t *segs, size_t n);
    /* optional: RTOS mutex around a transaction */
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} ep_spi_bus_ops_t;

typedef struct {
    const ep_spi_bus_ops_t *ops;
    void *ctx;
    const ep_clock_t *clock;  /* for segment delays; may be NULL */
    ep_spi_config_t active;   /* settings currently programmed */
    bool active_valid;
} ep_spi_bus_t;

typedef struct {
    ep_spi_bus_t *bus;
    uint8_t cs;
    ep_spi_config_t cfg;
} ep_spi_device_t;

ep_status_t ep_spi_bus_init(ep_spi_bus_t *bus, const ep_spi_bus_ops_t *ops, void *ctx,
                            const ep_clock_t *clock);
ep_status_t ep_spi_device_init(ep_spi_device_t *dev, ep_spi_bus_t *bus, uint8_t cs,
                               const ep_spi_config_t *cfg);

ep_status_t ep_spi_transfer_segments(ep_spi_device_t *dev, const ep_spi_segment_t *segs, size_t n);
ep_status_t ep_spi_transfer(ep_spi_device_t *dev, const uint8_t *tx, uint8_t *rx, size_t len);
ep_status_t ep_spi_write(ep_spi_device_t *dev, const uint8_t *tx, size_t len);
ep_status_t ep_spi_write_then_read(ep_spi_device_t *dev, const uint8_t *tx, size_t txlen,
                                   uint8_t *rx, size_t rxlen);

/* Common sensor/flash convention: first byte is (reg | read_flag). */
ep_status_t ep_spi_reg_read(ep_spi_device_t *dev, uint8_t reg, uint8_t read_flag,
                            uint8_t *buf, size_t len);
ep_status_t ep_spi_reg_write(ep_spi_device_t *dev, uint8_t reg, const uint8_t *buf, size_t len);

/* ------------------------------------------------------------------ */
/* Bit-banged master over GPIO (any mode, 8-bit words).                */
/* ------------------------------------------------------------------ */
#define EP_SPI_BB_MAX_CS 4

typedef struct {
    ep_gpio_t gpio;
    const ep_clock_t *clock;
    uint32_t sck, mosi, miso;
    uint32_t cs_pins[EP_SPI_BB_MAX_CS];
    uint32_t half_period_us; /* computed from max_hz by configure() */
    ep_spi_config_t cfg;
} ep_spi_bitbang_t;

extern const ep_spi_bus_ops_t ep_spi_bitbang_ops;

void ep_spi_bitbang_init(ep_spi_bitbang_t *bb, const ep_gpio_t *gpio, const ep_clock_t *clock,
                         uint32_t sck, uint32_t mosi, uint32_t miso,
                         const uint32_t *cs_pins, size_t n_cs);

#ifdef __cplusplus
}
#endif

#endif /* EP_SPI_H */
