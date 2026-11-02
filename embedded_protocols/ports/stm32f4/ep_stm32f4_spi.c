/*
 * STM32F4 SPI1 master backend for ep_spi_bus_t (polled, software CS).
 *
 * Chip selects are plain GPIO outputs (port, pin) so any number of devices
 * can share the bus; the C core reprograms CR1 only when the addressed
 * device needs a different mode/clock.
 */
#include "ep/ep_spi.h"
#include "ep_stm32f4_regs.h"

typedef struct {
    stm32_spi_t *spi;
    uint32_t pclk_hz;
    struct {
        uint8_t port; /* 0 = GPIOA ... */
        uint8_t pin;
    } cs[4];
} ep_stm32f4_spi_t;

static ep_status_t s_configure(void *ctx, const ep_spi_config_t *cfg)
{
    ep_stm32f4_spi_t *s = ctx;
    if (cfg->bits_per_word != 8 && cfg->bits_per_word != 16)
        return EP_ERR_UNSUPPORTED;
    /* SCK = PCLK / 2^(BR+1): pick the fastest that does not exceed max_hz */
    uint32_t br = 0;
    while (br < 7 && (s->pclk_hz >> (br + 1)) > cfg->max_hz)
        br++;
    uint32_t cr1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (br << 3);
    if (cfg->mode & 1) cr1 |= SPI_CR1_CPHA;
    if (cfg->mode & 2) cr1 |= SPI_CR1_CPOL;
    if (cfg->lsb_first) cr1 |= SPI_CR1_LSBFIRST;
    if (cfg->bits_per_word == 16) cr1 |= SPI_CR1_DFF;
    while (s->spi->SR & SPI_SR_BSY) {
    }
    s->spi->CR1 = 0;
    s->spi->CR1 = cr1 | SPI_CR1_SPE;
    return EP_OK;
}

static ep_status_t s_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    ep_stm32f4_spi_t *s = ctx;
    for (size_t i = 0; i < len; i++) {
        uint32_t guard = 1000000;
        while (!(s->spi->SR & SPI_SR_TXE))
            if (!--guard) return EP_ERR_TIMEOUT;
        s->spi->DR = tx ? tx[i] : 0x00;
        guard = 1000000;
        while (!(s->spi->SR & SPI_SR_RXNE))
            if (!--guard) return EP_ERR_TIMEOUT;
        uint8_t v = (uint8_t)s->spi->DR; /* always read: clears RXNE / avoids OVR */
        if (rx)
            rx[i] = v;
    }
    while (s->spi->SR & SPI_SR_BSY) {
    }
    return EP_OK;
}

static void s_chip_select(void *ctx, uint8_t cs, bool assert_cs, bool active_high)
{
    ep_stm32f4_spi_t *s = ctx;
    if (cs >= 4)
        return;
    stm32_gpio_t *g = STM32_GPIO(s->cs[cs].port);
    bool level = assert_cs == active_high;
    g->BSRR = level ? (1u << s->cs[cs].pin) : (1u << (s->cs[cs].pin + 16u)); /* atomic set/reset */
}

const ep_spi_bus_ops_t ep_stm32f4_spi_ops = {
    .configure = s_configure,
    .transfer = s_transfer,
    .chip_select = s_chip_select,
};

ep_stm32f4_spi_t ep_stm32f4_spi1 = {
    .spi = STM32_SPI1,
    .pclk_hz = 84000000u, /* APB2 at 168 MHz SYSCLK */
    .cs = {{0, 4}, {1, 6}, {1, 7}, {2, 9}}, /* PA4, PB6, PB7, PC9 */
};
