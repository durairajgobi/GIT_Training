/* SPDX-License-Identifier: MIT */
/*
 * test_devhal.c - Host unit tests for devhal.
 *
 * Character-device modules run against mock_sys.c; the sysfs PWM module
 * runs against a scratch directory tree that mimics /sys/class/pwm.
 */
#include "devhal/devhal.h"
#include "mock_sys.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;

#define CHECK(cond) do {                                                  \
	checks++;                                                         \
	if (!(cond)) {                                                    \
		failures++;                                               \
		fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#cond);                                           \
	}                                                                 \
} while (0)

#define CHECK_EQ(a, b) do {                                               \
	long long a_ = (long long)(a), b_ = (long long)(b);               \
	checks++;                                                         \
	if (a_ != b_) {                                                   \
		failures++;                                               \
		fprintf(stderr, "  FAIL %s:%d: %s == %lld, expected %lld\n",\
			__FILE__, __LINE__, #a, a_, b_);                  \
	}                                                                 \
} while (0)

#define RUN(fn) do {                                                      \
	int before_ = failures;                                           \
	mock_reset();                                                     \
	hal_set_sys_ops(&mock_ops);                                       \
	fn();                                                             \
	CHECK_EQ(mock.open_fds, 0);                                       \
	printf("%-36s %s\n", #fn, failures == before_ ? "ok" : "FAILED"); \
} while (0)

/* ------------------------------------------------------------- core ---- */

struct log_capture {
	int count;
	hal_log_level_t level;
	char module[16];
};

static void capture_log(hal_log_level_t lvl, const char *mod, const char *msg,
			void *user)
{
	struct log_capture *c = user;

	(void)msg;
	c->count++;
	c->level = lvl;
	snprintf(c->module, sizeof(c->module), "%s", mod);
}

static void test_core(void)
{
	struct log_capture cap = { 0 };
	hal_i2c_t *h;

	CHECK(strcmp(hal_version(), "1.0.0") == 0);
	CHECK(strcmp(hal_strerror(0), "Success") == 0);
	CHECK(strstr(hal_strerror(-ETIMEDOUT), "Timed out") != NULL);
	CHECK(strstr(hal_strerror(-ENXIO), "NAK") != NULL);

	hal_set_log_handler(capture_log, &cap);
	hal_set_log_level(HAL_LOG_ERROR);
	CHECK_EQ(hal_get_log_level(), HAL_LOG_ERROR);
	CHECK_EQ(hal_i2c_open(&h, 7), -ENOENT);   /* logs an error */
	CHECK(h == NULL);
	CHECK_EQ(cap.count, 1);
	CHECK_EQ(cap.level, HAL_LOG_ERROR);
	CHECK(strcmp(cap.module, "i2c") == 0);

	hal_set_log_level(HAL_LOG_NONE);
	CHECK_EQ(hal_i2c_open(&h, 7), -ENOENT);
	CHECK_EQ(cap.count, 1);                   /* filtered */
	hal_set_log_handler(NULL, NULL);
	hal_set_log_level(HAL_LOG_NONE);
}

/* -------------------------------------------------------------- I2C ---- */

static void test_i2c_basic(void)
{
	hal_i2c_t *h;
	uint8_t v = 0, buf[4];
	uint16_t w = 0;
	const uint8_t blk[4] = { 1, 2, 3, 4 };

	CHECK_EQ(hal_i2c_open(&h, 1), 0);

	CHECK_EQ(hal_i2c_reg_write8(h, MOCK_I2C_ADDR, 0x10, 0xAB), 0);
	CHECK_EQ(mock.i2c_regs[0x10], 0xAB);
	CHECK_EQ(hal_i2c_reg_read8(h, MOCK_I2C_ADDR, 0x10, &v), 0);
	CHECK_EQ(v, 0xAB);

	CHECK_EQ(hal_i2c_reg_update8(h, MOCK_I2C_ADDR, 0x10, 0x0F, 0x05), 0);
	CHECK_EQ(mock.i2c_regs[0x10], 0xA5);

	CHECK_EQ(hal_i2c_reg_write16be(h, MOCK_I2C_ADDR, 0x20, 0x1234), 0);
	CHECK_EQ(mock.i2c_regs[0x20], 0x12);
	CHECK_EQ(mock.i2c_regs[0x21], 0x34);
	CHECK_EQ(hal_i2c_reg_read16be(h, MOCK_I2C_ADDR, 0x20, &w), 0);
	CHECK_EQ(w, 0x1234);

	CHECK_EQ(hal_i2c_reg_write_block(h, MOCK_I2C_ADDR, 0x40, blk, 4), 0);
	memset(buf, 0, sizeof(buf));
	CHECK_EQ(hal_i2c_reg_read_block(h, MOCK_I2C_ADDR, 0x40, buf, 4), 0);
	CHECK(memcmp(buf, blk, 4) == 0);

	/* Combined write+read is a single I2C_RDWR (repeated start). */
	mock.i2c_xfers = 0;
	CHECK_EQ(hal_i2c_reg_read8(h, MOCK_I2C_ADDR, 0x41, &v), 0);
	CHECK_EQ(mock.i2c_xfers, 1);
	CHECK_EQ(v, 2);

	CHECK_EQ(hal_i2c_probe(h, MOCK_I2C_ADDR), 0);
	CHECK_EQ(hal_i2c_probe(h, 0x51), -ENXIO);

	CHECK_EQ(hal_i2c_close(h), 0);
}

static void test_i2c_errors_and_retries(void)
{
	hal_i2c_t *h;
	uint8_t v;
	hal_i2c_msg_t m = { 0x400, 0, 1, &v };

	CHECK_EQ(hal_i2c_open(&h, 1), 0);

	/* 10-bit address not supported by this adapter; > 0x3FF invalid. */
	m.addr = 0x123;
	CHECK_EQ(hal_i2c_transfer(h, &m, 1), -EOPNOTSUPP);
	m.addr = 0x400;
	CHECK_EQ(hal_i2c_transfer(h, &m, 1), -EINVAL);
	CHECK_EQ(hal_i2c_transfer(h, &m, 0), -EINVAL);
	CHECK_EQ(hal_i2c_read(h, MOCK_I2C_ADDR, &v, 0), -EINVAL);

	/* No retries by default. */
	mock.i2c_fail_next = 2;
	mock.i2c_xfers = 0;
	CHECK_EQ(hal_i2c_reg_write8(h, MOCK_I2C_ADDR, 0, 1), -EAGAIN);
	CHECK_EQ(mock.i2c_xfers, 1);

	/* Two retries absorb one remaining failure. */
	CHECK_EQ(hal_i2c_set_retries(h, 2, 0), 0);
	mock.i2c_xfers = 0;
	CHECK_EQ(hal_i2c_reg_write8(h, MOCK_I2C_ADDR, 0, 1), 0);
	CHECK_EQ(mock.i2c_xfers, 2);

	/* ...but give up after retries + 1 attempts. */
	mock.i2c_fail_next = 5;
	mock.i2c_xfers = 0;
	CHECK_EQ(hal_i2c_reg_write8(h, MOCK_I2C_ADDR, 0, 1), -EAGAIN);
	CHECK_EQ(mock.i2c_xfers, 3);

	/* Probe never retries. */
	mock.i2c_fail_next = 1;
	mock.i2c_xfers = 0;
	CHECK_EQ(hal_i2c_probe(h, MOCK_I2C_ADDR), -EAGAIN);
	CHECK_EQ(mock.i2c_xfers, 1);

	CHECK_EQ(hal_i2c_close(h), 0);

	/* Adapter without plain-I2C support is refused at open. */
	mock.i2c_funcs = 0;
	CHECK_EQ(hal_i2c_open(&h, 1), -EOPNOTSUPP);
}

static void test_i2c_write_cb(void)
{
	hal_i2c_t *h;
	const uint8_t cmd[2] = { 0x30, 0x99 };

	CHECK_EQ(hal_i2c_open(&h, 1), 0);
	CHECK_EQ(hal_i2c_write_cb(h, MOCK_I2C_ADDR, cmd, 2), 0);
	CHECK_EQ(mock.i2c_regs[0x30], 0x99);
	CHECK_EQ(hal_i2c_close(h), 0);
}

/* -------------------------------------------------------------- SPI ---- */

static void test_spi(void)
{
	hal_spi_config_t cfg = HAL_SPI_CONFIG_INIT, got;
	hal_spi_t *s;
	uint8_t tx[3] = { 0x9F, 0x01, 0x02 }, rx[3] = { 0 }, id[2];

	cfg.mode = 3;
	cfg.max_speed_hz = 5000000;
	CHECK_EQ(hal_spi_open(&s, 0, 0, &cfg), 0);
	CHECK_EQ(mock.spi_mode, 3);
	CHECK_EQ(mock.spi_bits, 8);
	CHECK_EQ(mock.spi_speed, 5000000);

	CHECK_EQ(hal_spi_transfer(s, tx, rx, 3), 0);
	CHECK(memcmp(tx, rx, 3) == 0);    /* loopback */

	CHECK_EQ(hal_spi_write_then_read(s, tx, 1, id, 2), 0);
	CHECK_EQ(mock.spi_last_nxfers, 2); /* one message, CS held */
	CHECK_EQ(id[0], 0xA5);
	CHECK_EQ(mock.spi_tx_len, 3);

	CHECK_EQ(hal_spi_read(s, NULL, 1), -EINVAL);
	CHECK_EQ(hal_spi_transfer(s, NULL, NULL, 1), -EINVAL);

	cfg.mode = 4;
	CHECK_EQ(hal_spi_configure(s, &cfg), -EINVAL);
	cfg.mode = 1;
	cfg.max_speed_hz = 100000;
	CHECK_EQ(hal_spi_configure(s, &cfg), 0);
	CHECK_EQ(hal_spi_get_config(s, &got), 0);
	CHECK_EQ(got.mode, 1);
	CHECK_EQ(got.max_speed_hz, 100000);
	CHECK_EQ(mock.spi_speed, 100000);

	CHECK_EQ(hal_spi_close(s), 0);
	CHECK_EQ(hal_spi_open(&s, 0, 1, NULL), -ENOENT);
}

/* ------------------------------------------------------------- GPIO ---- */

static void test_gpio_output(void)
{
	hal_gpio_config_t cfg = HAL_GPIO_CONFIG_INIT;
	hal_gpio_t *g;
	int v = -1;

	cfg.direction = HAL_GPIO_DIR_OUTPUT;
	cfg.drive = HAL_GPIO_DRIVE_OPEN_DRAIN;
	cfg.initial_value = 1;
	cfg.consumer = "status-led";
	CHECK_EQ(hal_gpio_open(&g, "gpiochip0", 17, &cfg), 0);
	CHECK_EQ(mock.gpio_offset, 17);
	CHECK(strcmp(mock.gpio_consumer, "status-led") == 0);
	CHECK(mock.gpio_flags & GPIO_V2_LINE_FLAG_OUTPUT);
	CHECK(mock.gpio_flags & GPIO_V2_LINE_FLAG_OPEN_DRAIN);
	CHECK_EQ(mock.gpio_out_value, 1);
	CHECK_EQ(mock.open_fds, 1);   /* chip fd already closed */

	CHECK_EQ(hal_gpio_set(g, 0), 0);
	CHECK_EQ(mock.gpio_out_value, 0);
	CHECK_EQ(hal_gpio_toggle(g), 0);
	CHECK_EQ(hal_gpio_get(g, &v), 0);
	CHECK_EQ(v, 1);

	/* Switch to input with pull-up: set() must now be refused. */
	cfg.direction = HAL_GPIO_DIR_INPUT;
	cfg.drive = HAL_GPIO_DRIVE_PUSH_PULL;
	cfg.bias = HAL_GPIO_BIAS_PULL_UP;
	CHECK_EQ(hal_gpio_reconfigure(g, &cfg), 0);
	CHECK(mock.gpio_flags & GPIO_V2_LINE_FLAG_INPUT);
	CHECK(mock.gpio_flags & GPIO_V2_LINE_FLAG_BIAS_PULL_UP);
	CHECK_EQ(hal_gpio_set(g, 1), -EPERM);

	CHECK_EQ(hal_gpio_close(g), 0);
}

static void test_gpio_validation(void)
{
	hal_gpio_config_t cfg = HAL_GPIO_CONFIG_INIT;
	hal_gpio_t *g = NULL;

	/* Edge detection on an output is meaningless. */
	cfg.direction = HAL_GPIO_DIR_OUTPUT;
	cfg.edge = HAL_GPIO_EDGE_RISING;
	CHECK_EQ(hal_gpio_open(&g, "gpiochip0", 1, &cfg), -EINVAL);
	/* Open drain on an input likewise. */
	cfg = (hal_gpio_config_t)HAL_GPIO_CONFIG_INIT;
	cfg.drive = HAL_GPIO_DRIVE_OPEN_DRAIN;
	CHECK_EQ(hal_gpio_open(&g, "gpiochip0", 1, &cfg), -EINVAL);
	cfg.drive = HAL_GPIO_DRIVE_PUSH_PULL;
	CHECK_EQ(hal_gpio_open(&g, "gpiochip9", 1, &cfg), -ENOENT);
	CHECK_EQ(hal_gpio_open(&g, "/dev/gpiochip0", 99, &cfg), -EINVAL);
	CHECK(g == NULL);
}

static void test_gpio_events(void)
{
	hal_gpio_config_t cfg = HAL_GPIO_CONFIG_INIT;
	hal_gpio_event_t ev;
	hal_gpio_t *g;

	cfg.edge = HAL_GPIO_EDGE_BOTH;
	cfg.debounce_us = 5000;
	cfg.active_low = true;
	CHECK_EQ(hal_gpio_open(&g, "/dev/gpiochip0", 4, &cfg), 0);
	CHECK(mock.gpio_flags & GPIO_V2_LINE_FLAG_EDGE_RISING);
	CHECK(mock.gpio_flags & GPIO_V2_LINE_FLAG_EDGE_FALLING);
	CHECK(mock.gpio_flags & GPIO_V2_LINE_FLAG_ACTIVE_LOW);
	CHECK_EQ(mock.gpio_debounce, 5000);
	CHECK(hal_gpio_fd(g) >= 0);

	CHECK_EQ(hal_gpio_wait_event(g, 0, &ev), -ETIMEDOUT);
	CHECK_EQ(hal_gpio_wait_event(g, 20, &ev), -ETIMEDOUT);

	mock_gpio_push_event(GPIO_V2_LINE_EVENT_RISING_EDGE);
	mock_gpio_push_event(GPIO_V2_LINE_EVENT_FALLING_EDGE);
	CHECK_EQ(hal_gpio_wait_event(g, 100, &ev), 0);
	CHECK_EQ(ev.edge, HAL_GPIO_EDGE_RISING);
	CHECK(ev.timestamp_ns > 0);
	CHECK_EQ(hal_gpio_wait_event(g, 100, &ev), 0);
	CHECK_EQ(ev.edge, HAL_GPIO_EDGE_FALLING);
	CHECK_EQ(hal_gpio_close(g), 0);

	/* Waiting on a line without edge detection is a usage error. */
	cfg = (hal_gpio_config_t)HAL_GPIO_CONFIG_INIT;
	CHECK_EQ(hal_gpio_open(&g, "gpiochip0", 4, &cfg), 0);
	CHECK_EQ(hal_gpio_wait_event(g, 0, &ev), -EPERM);
	CHECK_EQ(hal_gpio_close(g), 0);
}

/* ------------------------------------------------------------- UART ---- */

static void test_uart_config(void)
{
	hal_uart_config_t cfg = HAL_UART_CONFIG_INIT;
	hal_uart_t *u;

	CHECK_EQ(hal_uart_open(&u, "/dev/ttyMOCK0", &cfg), 0);
	CHECK_EQ(cfgetospeed(&mock.tio), B115200);
	CHECK_EQ(mock.tio.c_cflag & CSIZE, CS8);
	CHECK(!(mock.tio.c_cflag & PARENB));
	CHECK(!(mock.tio.c_lflag & ICANON));
	CHECK(mock.tio.c_cflag & CREAD);
	CHECK_EQ(mock.uart_flushes, 1);

	cfg.baud = 9600;
	cfg.data_bits = 7;
	cfg.parity = HAL_UART_PARITY_ODD;
	cfg.stop_bits = 2;
	cfg.rtscts = true;
	CHECK_EQ(hal_uart_configure(u, &cfg), 0);
	CHECK_EQ(cfgetispeed(&mock.tio), B9600);
	CHECK_EQ(mock.tio.c_cflag & CSIZE, CS7);
	CHECK(mock.tio.c_cflag & PARENB);
	CHECK(mock.tio.c_cflag & PARODD);
	CHECK(mock.tio.c_cflag & CSTOPB);
	CHECK(mock.tio.c_cflag & CRTSCTS);

	cfg.baud = 12345;
	CHECK_EQ(hal_uart_configure(u, &cfg), -EINVAL);
	cfg.baud = 9600;
	cfg.stop_bits = 3;
	CHECK_EQ(hal_uart_configure(u, &cfg), -EINVAL);

	CHECK_EQ(hal_uart_flush(u, HAL_UART_FLUSH_RX), 0);
	CHECK_EQ(hal_uart_drain(u), 0);
	CHECK_EQ(hal_uart_close(u), 0);
}

static void test_uart_io(void)
{
	hal_uart_t *u;
	char line[16];
	uint8_t buf[8];
	const char msg[] = "AT+CSQ\r\n";

	CHECK_EQ(hal_uart_open(&u, "/dev/ttyMOCK0", NULL), 0);

	/* write() loops over short writes until everything is queued. */
	mock.uart_write_chunk = 3;
	CHECK_EQ(hal_uart_write(u, msg, strlen(msg), 100), (ssize_t)strlen(msg));
	CHECK_EQ(mock.uart_tx_len, strlen(msg));
	CHECK(memcmp(mock.uart_tx, msg, strlen(msg)) == 0);

	mock.uart_tx_full = true;
	CHECK_EQ(hal_uart_write(u, "x", 1, 20), -ETIMEDOUT);
	mock.uart_tx_full = false;

	CHECK_EQ(hal_uart_read(u, buf, sizeof(buf), 20), -ETIMEDOUT);
	mock_uart_feed("OK", 2);
	CHECK_EQ(hal_uart_read(u, buf, sizeof(buf), 20), 2);
	CHECK(memcmp(buf, "OK", 2) == 0);

	mock_uart_feed("ABCDE", 5);
	CHECK_EQ(hal_uart_read_exact(u, buf, 4, 20), 4);
	CHECK(memcmp(buf, "ABCD", 4) == 0);
	CHECK_EQ(hal_uart_read_exact(u, buf, 4, 20), -ETIMEDOUT);  /* only E */

	mock_uart_feed("+CSQ: 21,99\r\nOK\r\n", 17);
	CHECK_EQ(hal_uart_read_line(u, line, sizeof(line), '\n', 50), 13);
	CHECK(strcmp(line, "+CSQ: 21,99\r\n") == 0);
	CHECK_EQ(hal_uart_read_line(u, line, sizeof(line), '\n', 50), 4);
	CHECK(strcmp(line, "OK\r\n") == 0);

	mock_uart_feed("0123456789", 10);
	CHECK_EQ(hal_uart_read_line(u, line, 6, '\n', 50), -ENOBUFS);
	CHECK(strcmp(line, "01234") == 0);

	mock_uart_feed("junk", 4);
	CHECK_EQ(hal_uart_flush(u, HAL_UART_FLUSH_BOTH), 0);
	CHECK_EQ(mock.uart_rx_len, 0);

	CHECK_EQ(hal_uart_close(u), 0);
}

/* -------------------------------------------------------- Backlight ---- */

static void test_backlight(void)
{
	hal_backlight_state_t st;
	hal_backlight_t *b;
	uint32_t lvl = 0;

	CHECK_EQ(hal_backlight_open(&b, NULL), 0);
	CHECK_EQ(hal_backlight_enable(b), 0);
	CHECK_EQ(mock.bl_enabled, 1);

	CHECK_EQ(hal_backlight_set_brightness(b, 128), 0);
	CHECK_EQ(hal_backlight_get_brightness(b, &lvl), 0);
	CHECK_EQ(lvl, 128);

	CHECK_EQ(hal_backlight_set_percent(b, 100), 0);
	CHECK_EQ(hal_backlight_get_brightness(b, &lvl), 0);
	CHECK_EQ(lvl, 255);
	CHECK_EQ(hal_backlight_set_percent(b, 50), 0);
	CHECK_EQ(hal_backlight_get_brightness(b, &lvl), 0);
	CHECK_EQ(lvl, 128);
	CHECK_EQ(hal_backlight_set_percent(b, 101), -EINVAL);

	CHECK_EQ(hal_backlight_set_pwm(b, 50000, 25000, true), 0);
	CHECK_EQ(hal_backlight_get_state(b, &st), 0);
	CHECK_EQ(st.period_ns, 50000);
	CHECK_EQ(st.duty_ns, 25000);
	CHECK(st.inversed);
	CHECK(st.enabled);
	CHECK_EQ(st.max_brightness, 255);
	CHECK_EQ(hal_backlight_set_pwm(b, 1000, 2000, false), -EINVAL);

	CHECK_EQ(hal_backlight_set_brightness(b, 300), -EINVAL);

	CHECK_EQ(hal_backlight_disable(b), 0);
	CHECK_EQ(mock.bl_enabled, 0);
	CHECK_EQ(hal_backlight_close(b), 0);
}

static void test_backlight_fade(void)
{
	hal_backlight_t *b;
	uint32_t lvl = 0;

	CHECK_EQ(hal_backlight_open(&b, "/dev/lcd_backlight"), 0);
	CHECK_EQ(hal_backlight_set_brightness(b, 10), 0);

	mock.bl_set_brightness_calls = 0;
	CHECK_EQ(hal_backlight_fade(b, 200, 10, 5), 0);
	CHECK_EQ(mock.bl_set_brightness_calls, 5);
	CHECK_EQ(hal_backlight_get_brightness(b, &lvl), 0);
	CHECK_EQ(lvl, 200);

	/* Downwards, steps capped to the distance. */
	mock.bl_set_brightness_calls = 0;
	CHECK_EQ(hal_backlight_fade(b, 197, 0, 50), 0);
	CHECK_EQ(mock.bl_set_brightness_calls, 3);
	CHECK_EQ(hal_backlight_get_brightness(b, &lvl), 0);
	CHECK_EQ(lvl, 197);

	/* No-op and out-of-range. */
	mock.bl_set_brightness_calls = 0;
	CHECK_EQ(hal_backlight_fade(b, 197, 10, 0), 0);
	CHECK_EQ(mock.bl_set_brightness_calls, 0);
	CHECK_EQ(hal_backlight_fade(b, 256, 10, 0), -EINVAL);

	CHECK_EQ(hal_backlight_close(b), 0);
}

/* -------------------------------------------------------------- PWM ---- */

static char sysroot[256];

static void put(const char *rel, const char *val)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s%s", sysroot, rel);
	f = fopen(path, "w");
	if (f) {
		fputs(val, f);
		fclose(f);
	}
}

static void get(const char *rel, char *out, size_t len)
{
	char path[512];
	FILE *f;

	out[0] = '\0';
	snprintf(path, sizeof(path), "%s%s", sysroot, rel);
	f = fopen(path, "r");
	if (f) {
		if (!fgets(out, (int)len, f))
			out[0] = '\0';
		fclose(f);
	}
}

static void mkdirs(const char *rel)
{
	char path[512], *p;

	snprintf(path, sizeof(path), "%s%s", sysroot, rel);
	for (p = path + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(path, 0755);
			*p = '/';
		}
	}
	mkdir(path, 0755);
}

static void make_channel(unsigned int ch)
{
	char dir[128], f[160];

	snprintf(dir, sizeof(dir), "/sys/class/pwm/pwmchip0/pwm%u", ch);
	mkdirs(dir);
	snprintf(f, sizeof(f), "%s/period", dir);
	put(f, "0\n");
	snprintf(f, sizeof(f), "%s/duty_cycle", dir);
	put(f, "0\n");
	snprintf(f, sizeof(f), "%s/enable", dir);
	put(f, "0\n");
	snprintf(f, sizeof(f), "%s/polarity", dir);
	put(f, "normal\n");
}

/* Plays the kernel: creates pwm1 once something is written to export. */
static void *fake_kernel(void *arg)
{
	char buf[16];
	int i;

	(void)arg;
	for (i = 0; i < 200; i++) {
		get("/sys/class/pwm/pwmchip0/export", buf, sizeof(buf));
		if (buf[0] == '1') {
			make_channel(1);
			break;
		}
		usleep(2000);
	}
	return NULL;
}

static void rm_tree(const char *path)
{
	DIR *d = opendir(path);
	struct dirent *e;
	char sub[768];

	if (!d) {
		unlink(path);
		return;
	}
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
		rm_tree(sub);
	}
	closedir(d);
	rmdir(path);
}

static void pwm_setup(void)
{
	const char *tmp = getenv("TMPDIR");

	hal_set_sys_ops(NULL);   /* real filesystem */
	snprintf(sysroot, sizeof(sysroot), "%s/devhal-sysfs-XXXXXX",
		 tmp ? tmp : "/tmp");
	if (!mkdtemp(sysroot)) {
		perror("mkdtemp");
		exit(1);
	}
	mkdirs("/sys/class/pwm/pwmchip0");
	put("/sys/class/pwm/pwmchip0/export", "");
	put("/sys/class/pwm/pwmchip0/unexport", "");
	make_channel(0);
	CHECK_EQ(hal_set_sysfs_root(sysroot), 0);
}

static void pwm_teardown(void)
{
	hal_set_sysfs_root(NULL);
	rm_tree(sysroot);
}

static void test_pwm(void)
{
	hal_pwm_state_t st, want;
	hal_pwm_t *p;
	char buf[32];

	pwm_setup();

	CHECK_EQ(hal_pwm_open(&p, 0, 0), 0);
	get("/sys/class/pwm/pwmchip0/export", buf, sizeof(buf));
	CHECK_EQ(buf[0], '\0');           /* already exported: left alone */

	want.period_ns = 1000000;
	want.duty_ns = 250000;
	want.polarity = HAL_PWM_POLARITY_INVERSED;
	want.enabled = true;
	CHECK_EQ(hal_pwm_apply(p, &want), 0);
	CHECK_EQ(hal_pwm_get_state(p, &st), 0);
	CHECK_EQ(st.period_ns, 1000000);
	CHECK_EQ(st.duty_ns, 250000);
	CHECK_EQ(st.polarity, HAL_PWM_POLARITY_INVERSED);
	CHECK(st.enabled);
	get("/sys/class/pwm/pwmchip0/pwm0/polarity", buf, sizeof(buf));
	CHECK(strcmp(buf, "inversed") == 0);

	CHECK_EQ(hal_pwm_set_duty_permille(p, 750), 0);
	CHECK_EQ(hal_pwm_get_state(p, &st), 0);
	CHECK_EQ(st.duty_ns, 750000);

	/* 2 kHz keeps the 75 % ratio. */
	CHECK_EQ(hal_pwm_set_frequency_hz(p, 2000), 0);
	CHECK_EQ(hal_pwm_get_state(p, &st), 0);
	CHECK_EQ(st.period_ns, 500000);
	CHECK_EQ(st.duty_ns, 375000);

	/* Shrinking the period below duty clamps duty. */
	CHECK_EQ(hal_pwm_set_period_ns(p, 100000), 0);
	CHECK_EQ(hal_pwm_get_state(p, &st), 0);
	CHECK_EQ(st.period_ns, 100000);
	CHECK_EQ(st.duty_ns, 100000);

	CHECK_EQ(hal_pwm_set_duty_ns(p, 200000), -EINVAL);
	CHECK_EQ(hal_pwm_set_duty_permille(p, 1001), -EINVAL);
	CHECK_EQ(hal_pwm_set_frequency_hz(p, 0), -EINVAL);

	CHECK_EQ(hal_pwm_disable(p), 0);
	get("/sys/class/pwm/pwmchip0/pwm0/enable", buf, sizeof(buf));
	CHECK_EQ(buf[0], '0');
	CHECK_EQ(hal_pwm_enable(p), 0);
	get("/sys/class/pwm/pwmchip0/pwm0/enable", buf, sizeof(buf));
	CHECK_EQ(buf[0], '1');

	CHECK_EQ(hal_pwm_close(p), 0);
	get("/sys/class/pwm/pwmchip0/unexport", buf, sizeof(buf));
	CHECK_EQ(buf[0], '\0');           /* we didn't export it */

	pwm_teardown();
}

static void test_pwm_export(void)
{
	pthread_t th;
	hal_pwm_t *p;
	char buf[16], buf2[512];

	pwm_setup();

	pthread_create(&th, NULL, fake_kernel, NULL);
	CHECK_EQ(hal_pwm_open(&p, 0, 1), 0);
	pthread_join(th, NULL);
	get("/sys/class/pwm/pwmchip0/export", buf, sizeof(buf));
	CHECK(strcmp(buf, "1") == 0);

	CHECK_EQ(hal_pwm_close(p), 0);
	get("/sys/class/pwm/pwmchip0/unexport", buf, sizeof(buf));
	CHECK(strcmp(buf, "1") == 0);      /* we exported it, so we undo it */

	/* detach() keeps the channel exported even though we exported it. */
	put("/sys/class/pwm/pwmchip0/unexport", "");
	rm_tree(strcat(strcpy(buf2, sysroot), "/sys/class/pwm/pwmchip0/pwm1"));
	put("/sys/class/pwm/pwmchip0/export", "");
	pthread_create(&th, NULL, fake_kernel, NULL);
	CHECK_EQ(hal_pwm_open(&p, 0, 1), 0);
	pthread_join(th, NULL);
	CHECK_EQ(hal_pwm_detach(p), 0);
	get("/sys/class/pwm/pwmchip0/unexport", buf, sizeof(buf));
	CHECK_EQ(buf[0], '\0');

	/* No such chip. */
	CHECK_EQ(hal_pwm_open(&p, 5, 0), -ENOENT);

	pwm_teardown();
}

/* ------------------------------------------------------ bad handles ---- */

static void test_i2c_handle_errors(void)
{
	uint8_t v;

	CHECK_EQ(hal_i2c_reg_read8(NULL, MOCK_I2C_ADDR, 0, &v), -EINVAL);
	CHECK_EQ(hal_i2c_close(NULL), -EINVAL);
	CHECK_EQ(hal_i2c_open(NULL, 1), -EINVAL);
	CHECK_EQ(hal_spi_close(NULL), -EINVAL);
	CHECK_EQ(hal_gpio_close(NULL), -EINVAL);
	CHECK_EQ(hal_uart_close(NULL), -EINVAL);
	CHECK_EQ(hal_pwm_close(NULL), -EINVAL);
	CHECK_EQ(hal_pwm_detach(NULL), -EINVAL);
	CHECK_EQ(hal_backlight_close(NULL), -EINVAL);
}

int main(void)
{
	hal_set_log_level(HAL_LOG_NONE);

	RUN(test_core);
	RUN(test_i2c_basic);
	RUN(test_i2c_errors_and_retries);
	RUN(test_i2c_write_cb);
	RUN(test_i2c_handle_errors);
	RUN(test_spi);
	RUN(test_gpio_output);
	RUN(test_gpio_validation);
	RUN(test_gpio_events);
	RUN(test_uart_config);
	RUN(test_uart_io);
	RUN(test_backlight);
	RUN(test_backlight_fade);
	RUN(test_pwm);
	RUN(test_pwm_export);

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
