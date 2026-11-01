/* SPDX-License-Identifier: MIT */
/*
 * mock_sys.h - Simulated /dev nodes for devhal host tests.
 *
 * Installed with hal_set_sys_ops(&mock_ops). Recognised paths:
 *
 *   /dev/i2c-1          I2C bus with a 256-byte register device at 0x50
 *   /dev/spidev0.0      SPI loopback (MISO = MOSI)
 *   /dev/gpiochip0      GPIO chip; line requests return line fds
 *   /dev/ttyMOCK0       UART with injectable RX bytes and captured TX
 *   /dev/lcd_backlight  pwm_lcd_bl driver model
 */
#ifndef MOCK_SYS_H
#define MOCK_SYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <termios.h>

#include "devhal/hal_core.h"

#define MOCK_I2C_ADDR 0x50

struct mock_state {
	int open_fds;               /* leak check */

	/* I2C */
	uint8_t  i2c_regs[256];
	uint8_t  i2c_ptr;
	int      i2c_fail_next;     /* fail this many transfers with EAGAIN */
	int      i2c_xfers;         /* I2C_RDWR calls seen */
	unsigned long i2c_funcs;

	/* SPI */
	uint8_t  spi_mode;
	uint8_t  spi_bits;
	uint32_t spi_speed;
	int      spi_msgs;          /* SPI_IOC_MESSAGE calls */
	int      spi_last_nxfers;
	uint8_t  spi_tx[256];       /* concatenated TX of last message */
	size_t   spi_tx_len;

	/* GPIO */
	uint64_t gpio_flags;        /* flags of last request / reconfig */
	uint32_t gpio_offset;
	char     gpio_consumer[32];
	uint32_t gpio_debounce;
	int      gpio_out_value;    /* last value driven */
	int      gpio_in_value;     /* value returned for reads */
	uint32_t gpio_ev_id[8];
	int      gpio_ev_count;

	/* UART */
	struct termios tio;
	uint8_t  uart_rx[512];
	size_t   uart_rx_len;
	uint8_t  uart_tx[512];
	size_t   uart_tx_len;
	size_t   uart_write_chunk;  /* max bytes accepted per write() */
	bool     uart_tx_full;      /* write() returns EAGAIN, poll never ready */
	int      uart_flushes;

	/* Backlight */
	uint64_t bl_period;
	uint64_t bl_duty;
	uint32_t bl_polarity;
	uint32_t bl_enabled;
	uint32_t bl_max;
	int      bl_set_brightness_calls;
};

extern struct mock_state mock;
extern const hal_sys_ops_t mock_ops;

void mock_reset(void);
void mock_gpio_push_event(uint32_t id);
void mock_uart_feed(const void *data, size_t len);

#endif /* MOCK_SYS_H */
