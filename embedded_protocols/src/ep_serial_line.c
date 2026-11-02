#include "ep/ep_serial_line.h"

#include <string.h>

void ep_line_config_default(ep_line_config_t *cfg, ep_line_mode_t mode)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = mode;
    cfg->de_pin = cfg->re_n_pin = cfg->term_pin = EP_PIN_NONE;
    cfg->dtr_pin = cfg->rts_pin = cfg->cts_pin = EP_PIN_NONE;
    cfg->dsr_pin = cfg->dcd_pin = cfg->ri_pin = EP_PIN_NONE;
    cfg->de_active_high = true;
}

static void set_de(ep_serial_line_t *l, bool on)
{
    ep_gpio_write(&l->cfg.gpio, l->cfg.de_pin, on == l->cfg.de_active_high);
}

static void set_rx_enabled(ep_serial_line_t *l, bool on)
{
    ep_gpio_write(&l->cfg.gpio, l->cfg.re_n_pin, !on); /* /RE active low */
}

static bool toggles_driver(const ep_serial_line_t *l)
{
    switch (l->cfg.mode) {
    case EP_LINE_RS485_HALF: return true;
    case EP_LINE_RS422:
    case EP_LINE_RS485_FULL: return l->cfg.multidrop;
    default: return false;
    }
}

static void release_driver(ep_serial_line_t *l)
{
    set_de(l, false);
    if (l->cfg.mode == EP_LINE_RS485_HALF)
        set_rx_enabled(l, true);
    l->release_at_ns = 0;
    l->driving = false;
}

/* --- ep_uart hooks ---------------------------------------------- */

static void hook_tx_start(void *arg)
{
    ep_serial_line_t *l = arg;
    if (!toggles_driver(l))
        return;
    l->release_at_ns = 0; /* cancel a pending release */
    if (!l->driving) {
        if (l->cfg.mode == EP_LINE_RS485_HALF && !l->cfg.echo_check)
            set_rx_enabled(l, false);
        set_de(l, true);
        l->driving = true;
        ep_clock_delay_us(l->clock, l->cfg.pre_delay_us);
    }
}

static void hook_tx_done(void *arg)
{
    ep_serial_line_t *l = arg;
    if (!toggles_driver(l))
        return;
    if (l->cfg.post_delay_us == 0 || !l->clock || !l->clock->now_ns)
        release_driver(l);
    else
        l->release_at_ns = ep_clock_now(l->clock) + (uint64_t)l->cfg.post_delay_us * 1000u;
}

static bool hook_rx_filter(void *arg, uint8_t b)
{
    ep_serial_line_t *l = arg;
    if (l->cfg.mode != EP_LINE_RS485_HALF)
        return false;
    if (!l->cfg.echo_check)
        return l->driving; /* receiver should be off anyway */
    uint8_t expect;
    if (ep_rb_get(&l->echo, &expect)) {
        if (expect != b)
            l->collisions++;
        return true; /* own echo: never deliver */
    }
    return false;
}

/* ---------------------------------------------------------------- */

ep_status_t ep_serial_line_init(ep_serial_line_t *line, ep_uart_t *uart,
                                const ep_line_config_t *cfg, const ep_clock_t *clock)
{
    if (!line || !uart || !cfg || cfg->mode > EP_LINE_RS485_FULL)
        return EP_ERR_PARAM;
    memset(line, 0, sizeof(*line));
    line->uart = uart;
    line->cfg = *cfg;
    line->clock = clock;
    ep_rb_init(&line->echo, line->echo_storage, sizeof(line->echo_storage));

    uart->on_tx_start = hook_tx_start;
    uart->on_tx_done = hook_tx_done;
    uart->rx_filter = hook_rx_filter;
    uart->hook_arg = line;

    switch (cfg->mode) {
    case EP_LINE_RS232:
        break;
    case EP_LINE_RS422:
    case EP_LINE_RS485_FULL:
        set_rx_enabled(line, true);
        set_de(line, !cfg->multidrop); /* point-to-point: drive permanently */
        line->driving = !cfg->multidrop;
        break;
    case EP_LINE_RS485_HALF:
        release_driver(line); /* idle in receive */
        break;
    }
    return EP_OK;
}

size_t ep_serial_line_send(ep_serial_line_t *line, const uint8_t *data, size_t len)
{
    if (line->cfg.mode == EP_LINE_RS485_HALF && line->cfg.echo_check) {
        /* only compare what fits; overflowed bytes are then just dropped */
        if (ep_rb_space(&line->echo) < len)
            len = ep_rb_space(&line->echo);
        ep_rb_write(&line->echo, data, len);
    }
    return ep_uart_write(line->uart, data, len);
}

void ep_serial_line_poll(ep_serial_line_t *line)
{
    uint64_t at = line->release_at_ns;
    if (at && ep_clock_now(line->clock) >= at && !ep_uart_tx_busy(line->uart))
        release_driver(line);
}

bool ep_serial_line_is_driving(const ep_serial_line_t *line)
{
    return line->driving;
}

void ep_serial_line_set_termination(ep_serial_line_t *line, bool on)
{
    ep_gpio_write(&line->cfg.gpio, line->cfg.term_pin, on);
}

void ep_serial_line_set_modem(ep_serial_line_t *line, uint8_t mask, uint8_t value)
{
    /* RS-232 drivers (MAX3232 etc.) invert: logic 1 at the UART = mark/-V.
     * Modem lines are "asserted" at +V, i.e. logic 0 on the TTL side. */
    if (mask & EP_MODEM_DTR)
        ep_gpio_write(&line->cfg.gpio, line->cfg.dtr_pin, !(value & EP_MODEM_DTR));
    if (mask & EP_MODEM_RTS)
        ep_gpio_write(&line->cfg.gpio, line->cfg.rts_pin, !(value & EP_MODEM_RTS));
}

uint8_t ep_serial_line_get_modem(const ep_serial_line_t *line)
{
    const ep_line_config_t *c = &line->cfg;
    uint8_t v = 0;
    if (c->cts_pin != EP_PIN_NONE && !ep_gpio_read(&c->gpio, c->cts_pin)) v |= EP_MODEM_CTS;
    if (c->dsr_pin != EP_PIN_NONE && !ep_gpio_read(&c->gpio, c->dsr_pin)) v |= EP_MODEM_DSR;
    if (c->dcd_pin != EP_PIN_NONE && !ep_gpio_read(&c->gpio, c->dcd_pin)) v |= EP_MODEM_DCD;
    if (c->ri_pin != EP_PIN_NONE && !ep_gpio_read(&c->gpio, c->ri_pin)) v |= EP_MODEM_RI;
    return v;
}

uint32_t ep_line_t35_us(const ep_uart_config_t *cfg)
{
    if (cfg->baud > 19200)
        return 1750; /* Modbus spec: fixed value above 19200 Bd */
    /* 3.5 character times, rounded up */
    return (uint32_t)((3500000ull * ep_uart_bits_per_char(cfg) + cfg->baud - 1) / cfg->baud);
}

uint32_t ep_line_t15_us(const ep_uart_config_t *cfg)
{
    if (cfg->baud > 19200)
        return 750;
    return (uint32_t)((1500000ull * ep_uart_bits_per_char(cfg) + cfg->baud - 1) / cfg->baud);
}
