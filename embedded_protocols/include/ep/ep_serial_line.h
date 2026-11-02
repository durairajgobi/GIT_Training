/*
 * ep_serial_line.h - Physical layer adapter for RS-232 / RS-422 / RS-485.
 *
 * Electrical differences the firmware has to care about:
 *
 *   RS-232       point-to-point, single-ended (+/-3..15 V). Optional modem
 *                control lines (DTR/RTS out, CTS/DSR/DCD/RI in).
 *   RS-422       full-duplex differential, one driver many receivers. A
 *                multidrop slave enables its driver only while answering.
 *   RS-485 half  2-wire, half-duplex multi-point. The driver (DE) must be
 *                enabled right before the first start bit and released
 *                right after the LAST STOP BIT (UART TC, not TXE!), else the
 *                last byte is truncated or the bus is blocked. The
 *                receiver (/RE) is either disabled while transmitting or
 *                left enabled and the own echo is discarded / compared to
 *                detect collisions.
 *   RS-485 full  4-wire, like RS-422 but with DE-controlled multidrop.
 *
 * The adapter plugs into ep_uart_t hooks so the UART driver itself stays
 * transceiver-agnostic.
 */
#ifndef EP_SERIAL_LINE_H
#define EP_SERIAL_LINE_H

#include "ep_common.h"
#include "ep_ringbuf.h"
#include "ep_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EP_LINE_RS232 = 0,
    EP_LINE_RS422,
    EP_LINE_RS485_HALF,
    EP_LINE_RS485_FULL
} ep_line_mode_t;

typedef struct {
    ep_line_mode_t mode;
    ep_gpio_t gpio;
    /* transceiver control (EP_PIN_NONE when not wired) */
    uint32_t de_pin;     /* driver enable                 */
    uint32_t re_n_pin;   /* receiver enable, active low   */
    uint32_t term_pin;   /* switchable 120R termination   */
    bool de_active_high;
    bool multidrop;      /* RS-422/485-full: drive only while sending */
    bool echo_check;     /* RS-485 half: keep RX on, compare own echo */
    uint32_t pre_delay_us;  /* DE asserted -> first start bit  */
    uint32_t post_delay_us; /* TC -> DE released               */
    /* RS-232 modem control */
    uint32_t dtr_pin, rts_pin, cts_pin, dsr_pin, dcd_pin, ri_pin;
} ep_line_config_t;

#define EP_MODEM_DTR 0x01u
#define EP_MODEM_RTS 0x02u
#define EP_MODEM_CTS 0x04u
#define EP_MODEM_DSR 0x08u
#define EP_MODEM_DCD 0x10u
#define EP_MODEM_RI  0x20u

typedef struct {
    ep_uart_t *uart;
    ep_line_config_t cfg;
    const ep_clock_t *clock;
    volatile bool driving;
    volatile uint64_t release_at_ns; /* pending DE release (post delay) */
    ep_ringbuf_t echo;
    uint8_t echo_storage[64];
    uint32_t collisions;
} ep_serial_line_t;

void ep_line_config_default(ep_line_config_t *cfg, ep_line_mode_t mode);
ep_status_t ep_serial_line_init(ep_serial_line_t *line, ep_uart_t *uart,
                                const ep_line_config_t *cfg, const ep_clock_t *clock);
size_t ep_serial_line_send(ep_serial_line_t *line, const uint8_t *data, size_t len);
/* Call from a timer / main loop when post_delay_us != 0. */
void ep_serial_line_poll(ep_serial_line_t *line);
bool ep_serial_line_is_driving(const ep_serial_line_t *line);
void ep_serial_line_set_termination(ep_serial_line_t *line, bool on);

void ep_serial_line_set_modem(ep_serial_line_t *line, uint8_t mask, uint8_t value);
uint8_t ep_serial_line_get_modem(const ep_serial_line_t *line);

/* Modbus RTU / generic inter-frame silence (t3.5) and inter-char (t1.5). */
uint32_t ep_line_t35_us(const ep_uart_config_t *cfg);
uint32_t ep_line_t15_us(const ep_uart_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* EP_SERIAL_LINE_H */
