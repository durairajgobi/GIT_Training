#include "ep/ep_uart.h"

#include <string.h>

ep_status_t ep_uart_init(ep_uart_t *u, const ep_uart_ops_t *ops, void *ctx,
                         const ep_uart_config_t *cfg,
                         uint8_t *rx_storage, size_t rx_size,
                         uint8_t *tx_storage, size_t tx_size)
{
    if (!u || !ops || !ops->tx_kick || !cfg || cfg->baud == 0 ||
        cfg->data_bits < 5 || cfg->data_bits > 9 || cfg->stop_bits < 1 || cfg->stop_bits > 2)
        return EP_ERR_PARAM;
    memset(u, 0, sizeof(*u));
    if (ep_rb_init(&u->rx, rx_storage, rx_size) != EP_OK ||
        ep_rb_init(&u->tx, tx_storage, tx_size) != EP_OK)
        return EP_ERR_PARAM;
    u->ops = ops;
    u->ctx = ctx;
    u->cfg = *cfg;
    return ops->configure ? ops->configure(ctx, cfg) : EP_OK;
}

static void crit_enter(ep_uart_t *u)
{
    if (u->ops->critical_enter)
        u->ops->critical_enter(u->ctx);
}

static void crit_exit(ep_uart_t *u)
{
    if (u->ops->critical_exit)
        u->ops->critical_exit(u->ctx);
}

size_t ep_uart_write(ep_uart_t *u, const uint8_t *data, size_t len)
{
    size_t n = ep_rb_write(&u->tx, data, len);
    if (n == 0)
        return 0;
    /* The ISR may be finishing the previous burst right now: decide whether
     * to (re)start the transmitter atomically with respect to TC. */
    crit_enter(u);
    bool start = !u->tx_active;
    if (start)
        u->tx_active = true;
    crit_exit(u);
    if (start) {
        if (u->on_tx_start)
            u->on_tx_start(u->hook_arg);
        u->ops->tx_kick(u->ctx);
    }
    return n;
}

size_t ep_uart_read(ep_uart_t *u, uint8_t *data, size_t len)
{
    return ep_rb_read(&u->rx, data, len);
}

size_t ep_uart_rx_available(const ep_uart_t *u)
{
    return ep_rb_count(&u->rx);
}

bool ep_uart_tx_busy(const ep_uart_t *u)
{
    return u->tx_active;
}

void ep_uart_flush_rx(ep_uart_t *u)
{
    ep_rb_reset(&u->rx);
}

void ep_uart_isr_rx(ep_uart_t *u, uint8_t byte, uint8_t err)
{
    if (err) {
        if (err & EP_UART_ERR_OVERRUN) u->stats.overrun++;
        if (err & EP_UART_ERR_FRAMING) u->stats.framing++;
        if (err & EP_UART_ERR_PARITY) u->stats.parity++;
        if (err & EP_UART_ERR_NOISE) u->stats.noise++;
        if (err & EP_UART_ERR_BREAK) u->stats.breaks++;
        /* a byte with framing/parity error is not delivered */
        if (err & (EP_UART_ERR_FRAMING | EP_UART_ERR_PARITY | EP_UART_ERR_BREAK))
            return;
    }
    if (u->rx_filter && u->rx_filter(u->hook_arg, byte))
        return;
    if (!ep_rb_put(&u->rx, byte))
        u->stats.rx_dropped++;
    else
        u->stats.rx_bytes++;
}

bool ep_uart_isr_tx_next(ep_uart_t *u, uint8_t *byte)
{
    if (!ep_rb_get(&u->tx, byte))
        return false;
    u->stats.tx_bytes++;
    return true;
}

void ep_uart_isr_tx_complete(ep_uart_t *u)
{
    if (ep_rb_count(&u->tx)) {
        /* more data was queued while the last byte was shifting out */
        u->ops->tx_kick(u->ctx);
        return;
    }
    u->tx_active = false;
    if (u->on_tx_done)
        u->on_tx_done(u->hook_arg);
}

unsigned ep_uart_bits_per_char(const ep_uart_config_t *cfg)
{
    return 1u + cfg->data_bits + (cfg->parity != EP_PARITY_NONE ? 1u : 0u) + cfg->stop_bits;
}

uint32_t ep_uart_char_time_ns(const ep_uart_config_t *cfg)
{
    return (uint32_t)(((uint64_t)ep_uart_bits_per_char(cfg) * 1000000000ull + cfg->baud / 2) /
                      cfg->baud);
}

ep_status_t ep_uart_calc_divisor(uint32_t clk_hz, uint32_t baud, uint8_t oversampling,
                                 uint8_t frac_bits, uint32_t *div_fixed, int32_t *err_ppm)
{
    if (!clk_hz || !baud || !oversampling || frac_bits > 8 || !div_fixed)
        return EP_ERR_PARAM;
    uint64_t num = (uint64_t)clk_hz << frac_bits;
    uint64_t den = (uint64_t)oversampling * baud;
    uint64_t div = (num + den / 2) / den;
    if (div == 0 || div > 0xFFFFFFFFull)
        return EP_ERR_PARAM;
    *div_fixed = (uint32_t)div;
    if (err_ppm) {
        /* actual = clk * 2^frac / (ovs * div) */
        double actual = (double)num / ((double)oversampling * (double)div);
        *err_ppm = (int32_t)((actual - baud) * 1e6 / baud);
    }
    return EP_OK;
}
