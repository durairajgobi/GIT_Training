/* SPDX-License-Identifier: MIT */
/*
 * devhalctl.c - Command-line front end for devhal, useful for board
 *               bring-up and as a reference for library users.
 *
 *   devhalctl i2c  <bus> scan
 *   devhalctl i2c  <bus> get <addr> <reg>
 *   devhalctl i2c  <bus> set <addr> <reg> <val>
 *   devhalctl gpio <chip> <line> get
 *   devhalctl gpio <chip> <line> set <0|1>
 *   devhalctl gpio <chip> <line> watch [count] [timeout_ms]
 *   devhalctl spi  <bus> <cs> xfer <byte>...
 *   devhalctl uart <dev> <baud> send <text> [reply_timeout_ms]
 *   devhalctl pwm  <chip> <ch> set <period_ns> <duty_ns>
 *   devhalctl pwm  <chip> <ch> off | show
 *   devhalctl bl   show | on | off | set <level> | pct <0..100>
 *   devhalctl bl   fade <level> <ms>
 *
 * Set DEVHAL_LOG=debug for library tracing.
 */
#include "devhal/devhal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
	fputs("usage:\n"
	      "  devhalctl i2c  <bus> scan | get <addr> <reg> | set <addr> <reg> <val>\n"
	      "  devhalctl gpio <chip> <line> get | set <0|1> | watch [count] [timeout_ms]\n"
	      "  devhalctl spi  <bus> <cs> xfer <byte>...\n"
	      "  devhalctl uart <dev> <baud> send <text> [reply_timeout_ms]\n"
	      "  devhalctl pwm  <chip> <ch> set <period_ns> <duty_ns> | off | show\n"
	      "  devhalctl bl   show | on | off | set <level> | pct <n> | fade <level> <ms>\n",
	      stderr);
	exit(2);
}

static unsigned long num(const char *s)
{
	char *end;
	unsigned long v = strtoul(s, &end, 0);

	if (end == s || *end) {
		fprintf(stderr, "not a number: %s\n", s);
		exit(2);
	}
	return v;
}

static int fail(const char *what, int r)
{
	fprintf(stderr, "%s: %s\n", what, hal_strerror(r));
	return 1;
}

static int cmd_i2c(int argc, char **argv)
{
	hal_i2c_t *h;
	int r, rc = 0;

	if (argc < 2)
		usage();
	r = hal_i2c_open(&h, (unsigned int)num(argv[0]));
	if (r < 0)
		return fail("open", r);

	if (!strcmp(argv[1], "scan")) {
		unsigned int a;

		printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
		for (a = 0; a < 0x80; a++) {
			if (a % 16 == 0)
				printf("\n%02x: ", a);
			if (a < 0x08 || a > 0x77)
				printf("   ");
			else if (hal_i2c_probe(h, (uint16_t)a) == 0)
				printf("%02x ", a);
			else
				printf("-- ");
		}
		printf("\n");
	} else if (!strcmp(argv[1], "get") && argc == 4) {
		uint8_t v;

		r = hal_i2c_reg_read8(h, (uint16_t)num(argv[2]),
				      (uint8_t)num(argv[3]), &v);
		if (r < 0)
			rc = fail("read", r);
		else
			printf("0x%02x\n", v);
	} else if (!strcmp(argv[1], "set") && argc == 5) {
		r = hal_i2c_reg_write8(h, (uint16_t)num(argv[2]),
				       (uint8_t)num(argv[3]),
				       (uint8_t)num(argv[4]));
		if (r < 0)
			rc = fail("write", r);
	} else {
		usage();
	}
	hal_i2c_close(h);
	return rc;
}

static int cmd_gpio(int argc, char **argv)
{
	hal_gpio_config_t cfg = HAL_GPIO_CONFIG_INIT;
	hal_gpio_t *g;
	int r, v, rc = 0;

	if (argc < 3)
		usage();
	cfg.consumer = "devhalctl";
	if (!strcmp(argv[2], "set")) {
		if (argc != 4)
			usage();
		cfg.direction = HAL_GPIO_DIR_OUTPUT;
		cfg.initial_value = (int)num(argv[3]);
	} else if (!strcmp(argv[2], "watch")) {
		cfg.edge = HAL_GPIO_EDGE_BOTH;
	} else if (strcmp(argv[2], "get")) {
		usage();
	}

	r = hal_gpio_open(&g, argv[0], (unsigned int)num(argv[1]), &cfg);
	if (r < 0)
		return fail("request line", r);

	if (!strcmp(argv[2], "get")) {
		r = hal_gpio_get(g, &v);
		if (r < 0)
			rc = fail("get", r);
		else
			printf("%d\n", v);
	} else if (!strcmp(argv[2], "watch")) {
		unsigned long count = argc > 3 ? num(argv[3]) : 0;
		int timeout = argc > 4 ? (int)num(argv[4]) : -1;
		unsigned long seen = 0;
		hal_gpio_event_t ev;

		while (count == 0 || seen < count) {
			r = hal_gpio_wait_event(g, timeout, &ev);
			if (r < 0) {
				rc = fail("wait", r);
				break;
			}
			printf("%" PRIu64 ".%09" PRIu64 " %s #%u\n",
			       ev.timestamp_ns / 1000000000u,
			       ev.timestamp_ns % 1000000000u,
			       ev.edge == HAL_GPIO_EDGE_RISING ? "rising"
							       : "falling",
			       ev.seqno);
			fflush(stdout);
			seen++;
		}
	}
	/* For "set" the value is applied by the request itself. Note that the
	 * kernel may return the line to its default once we release it. */
	hal_gpio_close(g);
	return rc;
}

static int cmd_spi(int argc, char **argv)
{
	uint8_t tx[256], rx[256];
	hal_spi_t *s;
	int i, n, r;

	if (argc < 4 || strcmp(argv[2], "xfer"))
		usage();
	n = argc - 3;
	if (n > (int)sizeof(tx))
		usage();
	for (i = 0; i < n; i++)
		tx[i] = (uint8_t)num(argv[3 + i]);

	r = hal_spi_open(&s, (unsigned int)num(argv[0]),
			 (unsigned int)num(argv[1]), NULL);
	if (r < 0)
		return fail("open", r);
	r = hal_spi_transfer(s, tx, rx, (size_t)n);
	hal_spi_close(s);
	if (r < 0)
		return fail("transfer", r);
	for (i = 0; i < n; i++)
		printf("%02x%c", rx[i], i == n - 1 ? '\n' : ' ');
	return 0;
}

static int cmd_uart(int argc, char **argv)
{
	hal_uart_config_t cfg = HAL_UART_CONFIG_INIT;
	hal_uart_t *u;
	char line[512];
	int timeout, r, rc = 0;
	ssize_t n;

	if (argc < 4 || strcmp(argv[2], "send"))
		usage();
	cfg.baud = (uint32_t)num(argv[1]);
	timeout = argc > 4 ? (int)num(argv[4]) : 1000;

	r = hal_uart_open(&u, argv[0], &cfg);
	if (r < 0)
		return fail("open", r);
	n = hal_uart_write(u, argv[3], strlen(argv[3]), 1000);
	if (n >= 0)
		n = hal_uart_write(u, "\r\n", 2, 1000);
	if (n < 0) {
		rc = fail("write", (int)n);
		goto out;
	}
	/* Print reply lines until the port goes quiet. */
	while ((n = hal_uart_read_line(u, line, sizeof(line), '\n',
				       timeout)) > 0)
		fputs(line, stdout);
	if (n != -ETIMEDOUT)
		rc = fail("read", (int)n);
out:
	hal_uart_close(u);
	return rc;
}

static int cmd_pwm(int argc, char **argv)
{
	hal_pwm_state_t st;
	hal_pwm_t *p;
	int r, rc = 0;

	if (argc < 3)
		usage();
	r = hal_pwm_open(&p, (unsigned int)num(argv[0]),
			 (unsigned int)num(argv[1]));
	if (r < 0)
		return fail("open", r);

	if (!strcmp(argv[2], "set") && argc == 5) {
		r = hal_pwm_get_state(p, &st);
		if (r == 0) {
			st.period_ns = num(argv[3]);
			st.duty_ns = num(argv[4]);
			st.enabled = true;
			r = hal_pwm_apply(p, &st);
		}
		if (r < 0)
			rc = fail("apply", r);
	} else if (!strcmp(argv[2], "off")) {
		r = hal_pwm_disable(p);
		if (r < 0)
			rc = fail("disable", r);
	} else if (!strcmp(argv[2], "show")) {
		r = hal_pwm_get_state(p, &st);
		if (r < 0)
			rc = fail("read", r);
		else
			printf("period %" PRIu64 " ns, duty %" PRIu64
			       " ns, %s, %s\n", st.period_ns, st.duty_ns,
			       st.polarity ? "inversed" : "normal",
			       st.enabled ? "enabled" : "disabled");
	} else {
		usage();
	}
	/* Leave the channel exported so the output keeps running. */
	hal_pwm_detach(p);
	return rc;
}

static int cmd_bl(int argc, char **argv)
{
	hal_backlight_state_t st;
	hal_backlight_t *b;
	int r;

	if (argc < 1)
		usage();
	r = hal_backlight_open(&b, NULL);
	if (r < 0)
		return fail("open", r);

	if (!strcmp(argv[0], "show")) {
		r = hal_backlight_get_state(b, &st);
		if (r == 0)
			printf("%s, brightness %u/%u, period %" PRIu64
			       " ns, duty %" PRIu64 " ns%s\n",
			       st.enabled ? "on" : "off", st.brightness,
			       st.max_brightness, st.period_ns, st.duty_ns,
			       st.inversed ? ", inversed" : "");
	} else if (!strcmp(argv[0], "on")) {
		r = hal_backlight_enable(b);
	} else if (!strcmp(argv[0], "off")) {
		r = hal_backlight_disable(b);
	} else if (!strcmp(argv[0], "set") && argc == 2) {
		r = hal_backlight_set_brightness(b, (uint32_t)num(argv[1]));
	} else if (!strcmp(argv[0], "pct") && argc == 2) {
		r = hal_backlight_set_percent(b, (unsigned int)num(argv[1]));
	} else if (!strcmp(argv[0], "fade") && argc == 3) {
		r = hal_backlight_fade(b, (uint32_t)num(argv[1]),
				       (unsigned int)num(argv[2]), 0);
	} else {
		usage();
	}
	hal_backlight_close(b);
	return r < 0 ? fail(argv[0], r) : 0;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		usage();
	if (!strcmp(argv[1], "i2c"))
		return cmd_i2c(argc - 2, argv + 2);
	if (!strcmp(argv[1], "gpio"))
		return cmd_gpio(argc - 2, argv + 2);
	if (!strcmp(argv[1], "spi"))
		return cmd_spi(argc - 2, argv + 2);
	if (!strcmp(argv[1], "uart"))
		return cmd_uart(argc - 2, argv + 2);
	if (!strcmp(argv[1], "pwm"))
		return cmd_pwm(argc - 2, argv + 2);
	if (!strcmp(argv[1], "bl"))
		return cmd_bl(argc - 2, argv + 2);
	if (!strcmp(argv[1], "--version")) {
		printf("devhal %s\n", hal_version());
		return 0;
	}
	usage();
	return 2;
}
