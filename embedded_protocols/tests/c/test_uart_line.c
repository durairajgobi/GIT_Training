/* UART driver + RS-232/422/485 physical layer, with a simulated USART. */
#include "ep/ep_serial_line.h"
#include "ep/ep_uart.h"
#include "ep_test.h"

enum { PIN_DE = 1, PIN_RE_N = 2, PIN_DTR = 3, PIN_RTS = 4, PIN_CTS = 5 };

typedef struct {
    bool pins[8];
    int de_changes;
    /* wire log: each byte with the DE level at the moment it left the UART */
    uint8_t wire[64];
    bool wire_de[64];
    size_t wire_len;
    bool kicked;
    bool loop_echo;    /* half duplex: own transmission appears on RX */
    uint8_t corrupt_at; /* 1-based index of echo byte to corrupt (0 = none) */
    ep_uart_t *uart;
} board_t;

static void g_write(void *ctx, uint32_t pin, bool v)
{
    board_t *b = ctx;
    if (pin == PIN_DE && b->pins[PIN_DE] != v)
        b->de_changes++;
    b->pins[pin] = v;
}
static bool g_read(void *ctx, uint32_t pin) { return ((board_t *)ctx)->pins[pin]; }

static void u_kick(void *ctx) { ((board_t *)ctx)->kicked = true; }
static const ep_uart_ops_t uart_ops = {.tx_kick = u_kick};

/* What the USART interrupt handler does: TXE until empty, then TC. */
static void run_tx_isr(board_t *b)
{
    while (b->kicked) {
        b->kicked = false;
        uint8_t byte;
        while (ep_uart_isr_tx_next(b->uart, &byte)) {
            b->wire_de[b->wire_len] = b->pins[PIN_DE];
            b->wire[b->wire_len++] = byte;
            if (b->loop_echo) {
                uint8_t echo = byte;
                if (b->corrupt_at && b->wire_len == b->corrupt_at)
                    echo ^= 0x01;
                ep_uart_isr_rx(b->uart, echo, 0);
            }
        }
        ep_uart_isr_tx_complete(b->uart);
    }
}

static uint64_t fake_now;
static uint64_t now_ns(void *ctx) { (void)ctx; return fake_now; }
static const ep_clock_t clk = {now_ns, NULL, NULL};

typedef struct {
    board_t board;
    ep_uart_t uart;
    uint8_t rxs[64], txs[64];
    ep_serial_line_t line;
} rig_t;

static void rig_init(rig_t *r, ep_line_mode_t mode, bool echo_check, uint32_t post_delay)
{
    memset(r, 0, sizeof(*r));
    r->board.uart = &r->uart;
    ep_uart_config_t cfg = EP_UART_CONFIG_8N1(115200);
    CHECK_EQ(ep_uart_init(&r->uart, &uart_ops, &r->board, &cfg, r->rxs, sizeof(r->rxs),
                          r->txs, sizeof(r->txs)), EP_OK);
    ep_line_config_t lc;
    ep_line_config_default(&lc, mode);
    ep_gpio_t g = {g_write, g_read, &r->board};
    lc.gpio = g;
    lc.de_pin = PIN_DE;
    lc.re_n_pin = PIN_RE_N;
    lc.dtr_pin = PIN_DTR;
    lc.rts_pin = PIN_RTS;
    lc.cts_pin = PIN_CTS;
    lc.echo_check = echo_check;
    lc.post_delay_us = post_delay;
    CHECK_EQ(ep_serial_line_init(&r->line, &r->uart, &lc, &clk), EP_OK);
}

static void test_rs485_half_duplex_turnaround(void)
{
    rig_t r;
    rig_init(&r, EP_LINE_RS485_HALF, false, 0);
    CHECK(!r.board.pins[PIN_DE]);    /* idle: listening */
    CHECK(!r.board.pins[PIN_RE_N]);  /* receiver enabled */

    const uint8_t msg[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    CHECK_EQ(ep_serial_line_send(&r.line, msg, sizeof(msg)), sizeof(msg));
    CHECK(r.board.pins[PIN_DE]);     /* driver on before the first byte */
    CHECK(r.board.pins[PIN_RE_N]);   /* receiver off while driving */
    run_tx_isr(&r.board);
    CHECK_EQ(r.board.wire_len, sizeof(msg));
    for (size_t i = 0; i < sizeof(msg); i++)
        CHECK(r.board.wire_de[i]);   /* every byte, incl. the last, sent with DE on */
    CHECK(!r.board.pins[PIN_DE]);    /* released on TC */
    CHECK(!r.board.pins[PIN_RE_N]);
    CHECK_EQ(r.board.de_changes, 2);
}

static void test_rs485_queued_during_tc_keeps_driver(void)
{
    rig_t r;
    rig_init(&r, EP_LINE_RS485_HALF, false, 0);
    const uint8_t a[] = {1, 2, 3}, b[] = {4, 5};
    ep_serial_line_send(&r.line, a, 3);
    /* first burst drained, more data queued before TC fires */
    uint8_t byte;
    while (ep_uart_isr_tx_next(&r.uart, &byte)) {}
    r.board.kicked = false;
    ep_serial_line_send(&r.line, b, 2); /* tx_active still true: no restart */
    ep_uart_isr_tx_complete(&r.uart);   /* sees pending data -> re-kicks */
    CHECK(r.board.pins[PIN_DE]);
    CHECK(r.board.kicked);
    run_tx_isr(&r.board);
    CHECK(!r.board.pins[PIN_DE]);
    CHECK_EQ(r.board.de_changes, 2);    /* one continuous drive window */
}

static void test_rs485_post_delay(void)
{
    rig_t r;
    rig_init(&r, EP_LINE_RS485_HALF, false, 50);
    fake_now = 1000000;
    const uint8_t m[] = {0xAA};
    ep_serial_line_send(&r.line, m, 1);
    run_tx_isr(&r.board);
    CHECK(r.board.pins[PIN_DE]);  /* still driving during post delay */
    fake_now += 49000;
    ep_serial_line_poll(&r.line);
    CHECK(r.board.pins[PIN_DE]);
    fake_now += 1000;
    ep_serial_line_poll(&r.line);
    CHECK(!r.board.pins[PIN_DE]);
}

static void test_rs485_echo_and_collision(void)
{
    rig_t r;
    rig_init(&r, EP_LINE_RS485_HALF, true, 0);
    r.board.loop_echo = true;
    const uint8_t m[] = {0x10, 0x20, 0x30};
    ep_serial_line_send(&r.line, m, 3);
    CHECK(!r.board.pins[PIN_RE_N]); /* echo mode keeps the receiver on */
    run_tx_isr(&r.board);
    CHECK_EQ(ep_uart_rx_available(&r.uart), 0); /* own echo never delivered */
    CHECK_EQ(r.line.collisions, 0);

    /* a peer answers: delivered normally */
    ep_uart_isr_rx(&r.uart, 0x77, 0);
    uint8_t got;
    CHECK_EQ(ep_uart_read(&r.uart, &got, 1), 1);
    CHECK_EQ(got, 0x77);

    /* second node talking at the same time corrupts our echo */
    r.board.wire_len = 0;
    r.board.corrupt_at = 2;
    ep_serial_line_send(&r.line, m, 3);
    run_tx_isr(&r.board);
    CHECK_EQ(r.line.collisions, 1);
}

static void test_rs422_point_to_point(void)
{
    rig_t r;
    rig_init(&r, EP_LINE_RS422, false, 0);
    CHECK(r.board.pins[PIN_DE]); /* driver permanently on */
    const uint8_t m[] = {1, 2};
    ep_serial_line_send(&r.line, m, 2);
    run_tx_isr(&r.board);
    CHECK(r.board.pins[PIN_DE]);
    CHECK_EQ(r.board.de_changes, 1);
}

static void test_rs232_modem_lines(void)
{
    rig_t r;
    rig_init(&r, EP_LINE_RS232, false, 0);
    r.board.pins[PIN_DTR] = r.board.pins[PIN_RTS] = true;
    ep_serial_line_set_modem(&r.line, EP_MODEM_DTR | EP_MODEM_RTS, EP_MODEM_DTR);
    CHECK(!r.board.pins[PIN_DTR]); /* asserted = TTL low through the inverter */
    CHECK(r.board.pins[PIN_RTS]);
    r.board.pins[PIN_CTS] = false;
    CHECK(ep_serial_line_get_modem(&r.line) & EP_MODEM_CTS);
    r.board.pins[PIN_CTS] = true;
    CHECK(!(ep_serial_line_get_modem(&r.line) & EP_MODEM_CTS));
}

static void test_rx_errors(void)
{
    rig_t r;
    rig_init(&r, EP_LINE_RS232, false, 0);
    ep_uart_isr_rx(&r.uart, 0x55, EP_UART_ERR_FRAMING);
    ep_uart_isr_rx(&r.uart, 0x56, EP_UART_ERR_PARITY);
    ep_uart_isr_rx(&r.uart, 0x57, EP_UART_ERR_OVERRUN); /* byte itself is fine */
    CHECK_EQ(r.uart.stats.framing, 1);
    CHECK_EQ(r.uart.stats.parity, 1);
    CHECK_EQ(r.uart.stats.overrun, 1);
    CHECK_EQ(ep_uart_rx_available(&r.uart), 1);
    for (int i = 0; i < 70; i++)
        ep_uart_isr_rx(&r.uart, (uint8_t)i, 0);
    CHECK_EQ(r.uart.stats.rx_dropped, 70 - 63);
}

static void test_timing_helpers(void)
{
    ep_uart_config_t c = EP_UART_CONFIG_8N1(9600);
    CHECK_EQ(ep_uart_bits_per_char(&c), 10);
    CHECK_EQ(ep_line_t35_us(&c), 3646);   /* 3.5 * 10 bits / 9600 Bd */
    CHECK_EQ(ep_line_t15_us(&c), 1563);
    c.baud = 115200;
    CHECK_EQ(ep_line_t35_us(&c), 1750);   /* fixed above 19200 Bd */
    ep_uart_config_t c8e1 = {19200, 8, EP_PARITY_EVEN, 1, EP_FLOW_NONE};
    CHECK_EQ(ep_uart_bits_per_char(&c8e1), 11);
    CHECK_EQ(ep_uart_char_time_ns(&c8e1), 572917);

    uint32_t div;
    int32_t err;
    CHECK_EQ(ep_uart_calc_divisor(84000000, 115200, 16, 4, &div, &err), EP_OK);
    CHECK_EQ(div, 729);           /* STM32 BRR = 0x2D9 (45.5625) */
    CHECK(err > 0 && err < 300);  /* +228 ppm */
    CHECK_EQ(ep_uart_calc_divisor(1843200, 9600, 16, 0, &div, &err), EP_OK);
    CHECK_EQ(div, 12);            /* classic 16550 divisor */
    CHECK_EQ(err, 0);
}

int main(void)
{
    RUN(test_rs485_half_duplex_turnaround);
    RUN(test_rs485_queued_during_tc_keeps_driver);
    RUN(test_rs485_post_delay);
    RUN(test_rs485_echo_and_collision);
    RUN(test_rs422_point_to_point);
    RUN(test_rs232_modem_lines);
    RUN(test_rx_errors);
    RUN(test_timing_helpers);
    TEST_MAIN_END();
}
