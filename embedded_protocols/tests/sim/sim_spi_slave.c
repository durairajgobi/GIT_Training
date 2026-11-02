#include "sim_spi_slave.h"

#include <string.h>

static uint8_t tx_byte(const sim_spi_slave_t *s)
{
    return s->byte_idx < s->tx_len ? s->tx[s->byte_idx] : 0xFF;
}

static void drive_miso(sim_spi_slave_t *s)
{
    s->miso = (tx_byte(s) >> (7 - s->bit)) & 1;
}

static void gpio_write(void *ctx, uint32_t pin, bool level)
{
    sim_spi_slave_t *s = ctx;
    switch (pin) {
    case SIM_PIN_CS0:
        if (s->cs_n && !level) { /* selected: present first bit (mode 0) */
            s->cs_assertions++;
            s->bit = 0;
            s->shift_in = 0;
            drive_miso(s);
        }
        s->cs_n = level;
        break;
    case SIM_PIN_MOSI:
        s->mosi = level;
        break;
    case SIM_PIN_SCK:
        if (s->cs_n) {
            s->sck = level;
            break;
        }
        if (!s->sck && level) { /* rising: sample */
            s->shift_in = (uint8_t)((s->shift_in << 1) | (s->mosi ? 1 : 0));
            if (++s->bit == 8) {
                if (s->rx_len < sizeof(s->rx))
                    s->rx[s->rx_len++] = s->shift_in;
                s->bit = 0;
                s->shift_in = 0;
                s->byte_idx++;
            }
        } else if (s->sck && !level) { /* falling: shift out next bit */
            drive_miso(s);
        }
        s->sck = level;
        break;
    default:
        break;
    }
}

static bool gpio_read(void *ctx, uint32_t pin)
{
    sim_spi_slave_t *s = ctx;
    return pin == SIM_PIN_MISO ? s->miso : false;
}

void sim_spi_slave_init(sim_spi_slave_t *s, const uint8_t *tx, size_t tx_len)
{
    memset(s, 0, sizeof(*s));
    s->cs_n = true;
    if (tx_len > sizeof(s->tx))
        tx_len = sizeof(s->tx);
    memcpy(s->tx, tx, tx_len);
    s->tx_len = tx_len;
}

ep_gpio_t sim_spi_slave_gpio(sim_spi_slave_t *s)
{
    ep_gpio_t g = {gpio_write, gpio_read, s};
    return g;
}
