/*
 * ep_uart.h - Interrupt-driven UART driver core.
 *
 * Data flow:
 *   task  --ep_uart_write()-->  tx ring  --ep_uart_isr_tx_next()-->  TX data reg
 *   RX data reg --ep_uart_isr_rx()--> rx ring --ep_uart_read()--> task
 *
 * The port's TXE interrupt handler pulls bytes with ep_uart_isr_tx_next()
 * until it returns false, disables TXE and enables TC; the TC handler calls
 * ep_uart_isr_tx_complete(). That last call is where an RS-485 transceiver
 * is turned around (see ep_serial_line.h).
 */
#ifndef EP_UART_H
#define EP_UART_H

#include "ep_common.h"
#include "ep_ringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { EP_PARITY_NONE = 0, EP_PARITY_ODD, EP_PARITY_EVEN } ep_parity_t;
typedef enum { EP_FLOW_NONE = 0, EP_FLOW_RTSCTS, EP_FLOW_XONXOFF } ep_flow_t;

typedef struct {
    uint32_t baud;
    uint8_t data_bits; /* 5..9 */
    ep_parity_t parity;
    uint8_t stop_bits; /* 1 or 2 */
    ep_flow_t flow;
} ep_uart_config_t;

#define EP_UART_CONFIG_8N1(b) {(b), 8, EP_PARITY_NONE, 1, EP_FLOW_NONE}

/* RX error flags reported by the port with each byte. */
#define EP_UART_ERR_OVERRUN 0x01u
#define EP_UART_ERR_FRAMING 0x02u
#define EP_UART_ERR_PARITY  0x04u
#define EP_UART_ERR_NOISE   0x08u
#define EP_UART_ERR_BREAK   0x10u

typedef struct {
    ep_status_t (*configure)(void *ctx, const ep_uart_config_t *cfg);
    void (*tx_kick)(void *ctx);                 /* enable TXE irq / start DMA   */
    void (*critical_enter)(void *ctx);          /* optional: mask UART irq      */
    void (*critical_exit)(void *ctx);
} ep_uart_ops_t;

typedef struct {
    uint32_t rx_bytes, tx_bytes;
    uint32_t overrun, framing, parity, noise, breaks;
    uint32_t rx_dropped; /* rx ring full */
} ep_uart_stats_t;

typedef struct ep_uart {
    const ep_uart_ops_t *ops;
    void *ctx;
    ep_uart_config_t cfg;
    ep_ringbuf_t rx, tx;
    volatile bool tx_active;
    ep_uart_stats_t stats;

    /* Hooks used by the physical-layer (RS-485) adapter. */
    void (*on_tx_start)(void *arg);           /* task ctx, before first byte */
    void (*on_tx_done)(void *arg);            /* ISR ctx, last stop bit sent */
    bool (*rx_filter)(void *arg, uint8_t b);  /* ISR ctx, return true = drop */
    void *hook_arg;
} ep_uart_t;

ep_status_t ep_uart_init(ep_uart_t *u, const ep_uart_ops_t *ops, void *ctx,
                         const ep_uart_config_t *cfg,
                         uint8_t *rx_storage, size_t rx_size,
                         uint8_t *tx_storage, size_t tx_size);
size_t ep_uart_write(ep_uart_t *u, const uint8_t *data, size_t len);
size_t ep_uart_read(ep_uart_t *u, uint8_t *data, size_t len);
size_t ep_uart_rx_available(const ep_uart_t *u);
bool ep_uart_tx_busy(const ep_uart_t *u);
void ep_uart_flush_rx(ep_uart_t *u);

/* Called by the port from interrupt context. */
void ep_uart_isr_rx(ep_uart_t *u, uint8_t byte, uint8_t err_flags);
bool ep_uart_isr_tx_next(ep_uart_t *u, uint8_t *byte);
void ep_uart_isr_tx_complete(ep_uart_t *u);

/* Helpers ---------------------------------------------------------- */

/* Bits on the wire per character (start + data + parity + stop). */
unsigned ep_uart_bits_per_char(const ep_uart_config_t *cfg);
uint32_t ep_uart_char_time_ns(const ep_uart_config_t *cfg);

/* Fixed-point baud divisor: div = clk / (oversampling * baud) with
 * `frac_bits` fractional bits (STM32 OVER16: 4, 16550: 0). Returns the
 * achieved error in ppm (signed) through err_ppm. */
ep_status_t ep_uart_calc_divisor(uint32_t clk_hz, uint32_t baud, uint8_t oversampling,
                                 uint8_t frac_bits, uint32_t *div_fixed, int32_t *err_ppm);

#ifdef __cplusplus
}
#endif

#endif /* EP_UART_H */
