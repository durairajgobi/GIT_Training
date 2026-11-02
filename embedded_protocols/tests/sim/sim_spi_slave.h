/* Bit-level SPI slave model driven by GPIO edges (modes 0 and 3). */
#ifndef SIM_SPI_SLAVE_H
#define SIM_SPI_SLAVE_H

#include "ep/ep_common.h"

enum { SIM_PIN_SCK = 0, SIM_PIN_MOSI = 1, SIM_PIN_MISO = 2, SIM_PIN_CS0 = 3 };

typedef struct {
    bool sck, mosi, miso, cs_n;
    uint8_t tx[64];   /* bytes the slave shifts out */
    size_t tx_len;
    uint8_t rx[64];   /* bytes the slave received   */
    size_t rx_len;
    uint8_t shift_in;
    int bit;
    size_t byte_idx;
    unsigned cs_assertions;
} sim_spi_slave_t;

void sim_spi_slave_init(sim_spi_slave_t *s, const uint8_t *tx, size_t tx_len);
ep_gpio_t sim_spi_slave_gpio(sim_spi_slave_t *s);

#endif
