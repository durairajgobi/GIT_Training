/* SPDX-License-Identifier: MIT */
/*
 * hal_uart.h - Serial ports (/dev/ttyS*, /dev/ttyUSB*, /dev/ttyAMA*...).
 *
 * The port is opened non-blocking and put in raw mode; every read/write
 * takes an explicit timeout so callers never hang on a dead peer.
 */
#ifndef DEVHAL_HAL_UART_H
#define DEVHAL_HAL_UART_H

#include "hal_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_uart hal_uart_t;

typedef enum {
	HAL_UART_PARITY_NONE = 0,
	HAL_UART_PARITY_ODD,
	HAL_UART_PARITY_EVEN,
} hal_uart_parity_t;

typedef enum {
	HAL_UART_FLUSH_RX = 1,
	HAL_UART_FLUSH_TX = 2,
	HAL_UART_FLUSH_BOTH = 3,
} hal_uart_flush_t;

typedef struct {
	uint32_t          baud;      /* standard rates 50..4000000 */
	uint8_t           data_bits; /* 5..8 */
	hal_uart_parity_t parity;
	uint8_t           stop_bits; /* 1 or 2 */
	bool              rtscts;    /* hardware flow control */
} hal_uart_config_t;

#define HAL_UART_CONFIG_INIT { 115200u, 8, HAL_UART_PARITY_NONE, 1, false }

int hal_uart_open(hal_uart_t **out, const char *path,
		  const hal_uart_config_t *cfg);
int hal_uart_close(hal_uart_t *uart);
int hal_uart_configure(hal_uart_t *uart, const hal_uart_config_t *cfg);

/*
 * Write all @len bytes. Returns @len, -ETIMEDOUT if the port did not accept
 * everything within @timeout_ms (< 0 = forever), or another error.
 */
ssize_t hal_uart_write(hal_uart_t *uart, const void *buf, size_t len,
		       int timeout_ms);

/*
 * Wait up to @timeout_ms for data, then return whatever is available
 * (1..len bytes). Returns -ETIMEDOUT if nothing arrived.
 */
ssize_t hal_uart_read(hal_uart_t *uart, void *buf, size_t len, int timeout_ms);

/* Read exactly @len bytes within a total budget of @timeout_ms. */
ssize_t hal_uart_read_exact(hal_uart_t *uart, void *buf, size_t len,
			    int timeout_ms);

/*
 * Read up to and including @delim (e.g. '\n'), NUL-terminating the result.
 * Returns the line length, -ETIMEDOUT, or -ENOBUFS if @len was too small.
 */
ssize_t hal_uart_read_line(hal_uart_t *uart, char *buf, size_t len,
			   char delim, int timeout_ms);

int hal_uart_flush(hal_uart_t *uart, hal_uart_flush_t which);
int hal_uart_drain(hal_uart_t *uart);   /* wait until TX FIFO is empty */
int hal_uart_fd(const hal_uart_t *uart);

#ifdef __cplusplus
}
#endif

#endif /* DEVHAL_HAL_UART_H */
