/* SPDX-License-Identifier: MIT */
/*
 * hal_spi.h - SPI master access via spidev (/dev/spidevB.C).
 */
#ifndef DEVHAL_HAL_SPI_H
#define DEVHAL_HAL_SPI_H

#include "hal_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_spi hal_spi_t;

typedef struct {
	uint8_t  mode;          /* 0..3 (CPOL/CPHA) */
	uint8_t  bits_per_word; /* 0 = 8 */
	uint32_t max_speed_hz;
	bool     lsb_first;
	bool     cs_high;
} hal_spi_config_t;

#define HAL_SPI_CONFIG_INIT { 0, 8, 1000000u, false, false }

/* One segment of a multi-part transfer; CS stays asserted between segments
 * unless @cs_change is set. tx or rx may be NULL (not both). */
typedef struct {
	const void *tx;
	void       *rx;
	uint32_t    len;
	uint32_t    speed_hz;    /* 0 = handle default */
	uint16_t    delay_us;    /* after this segment */
	bool        cs_change;
} hal_spi_xfer_t;

#define HAL_SPI_MAX_XFERS 64

int hal_spi_open(hal_spi_t **out, unsigned int bus, unsigned int cs,
		 const hal_spi_config_t *cfg);
int hal_spi_open_path(hal_spi_t **out, const char *path,
		      const hal_spi_config_t *cfg);
int hal_spi_close(hal_spi_t *spi);

int hal_spi_configure(hal_spi_t *spi, const hal_spi_config_t *cfg);
int hal_spi_get_config(hal_spi_t *spi, hal_spi_config_t *cfg);

/* Full duplex: clocks @len bytes out of @tx while reading into @rx. */
int hal_spi_transfer(hal_spi_t *spi, const void *tx, void *rx, size_t len);
int hal_spi_write(hal_spi_t *spi, const void *tx, size_t len);
int hal_spi_read(hal_spi_t *spi, void *rx, size_t len);
/* Half duplex command/response with CS held across both phases. */
int hal_spi_write_then_read(hal_spi_t *spi, const void *tx, size_t txlen,
			    void *rx, size_t rxlen);
int hal_spi_transfer_multi(hal_spi_t *spi, const hal_spi_xfer_t *xfers,
			   size_t count);

#ifdef __cplusplus
}
#endif

#endif /* DEVHAL_HAL_SPI_H */
