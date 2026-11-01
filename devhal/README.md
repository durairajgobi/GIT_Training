# devhal — user-space device middleware for Linux

`devhal` is a C11 library that gives applications one consistent API for
Linux device drivers. Applications don't need to make raw `ioctl()`,
`termios` or sysfs calls themselves.

```
 ┌──────────────────────────────────────────────────────────────┐
 │  Applications / services (eCall, PM daemon, HMI, test tools) │
 └──────────────────────────────┬───────────────────────────────┘
                                │  devhal C API  (#include <devhal/devhal.h>)
 ┌──────┬──────┬──────┬──────┬──┴───┬───────────┬────────────────┐
 │ GPIO │ I2C  │ SPI  │ UART │ PWM  │ Backlight │ core: errors,  │
 │      │      │      │      │      │           │ logging, time  │
 └──┬───┴──┬───┴──┬───┴──┬───┴──┬───┴─────┬─────┴────────────────┘
    │      │      │      │      │         │   hal_sys_ops_t (swappable)
 /dev/   /dev/  /dev/   /dev/  /sys/    /dev/
 gpiochipN i2c-N spidevB.C tty* class/pwm lcd_backlight  ← kernel drivers
```

| Module        | Kernel interface                           | Highlights |
|---------------|--------------------------------------------|------------|
| `hal_gpio`    | GPIO char device, uAPI v2                  | bias, open-drain/source, active-low, debounce, edge events with timeouts, fd for epoll |
| `hal_i2c`     | `/dev/i2c-N`, `I2C_RDWR`                   | repeated-start write/read, 8/16-bit register helpers, read-modify-write, 10-bit addresses, retries, bus scan |
| `hal_spi`     | `spidev`, `SPI_IOC_MESSAGE(n)`             | full-duplex, command/response with CS held, multi-segment transfers |
| `hal_uart`    | tty + termios                              | raw mode, 50 baud–4 Mbaud, parity/stop/RTS-CTS, timed read/write, `read_exact`, `read_line` |
| `hal_pwm`     | `/sys/class/pwm`                           | auto export + udev wait, writes always keep duty ≤ period, polarity, frequency/permille helpers |
| `hal_backlight` | `/dev/lcd_backlight` ([`../pwm_lcd_backlight`](../pwm_lcd_backlight)) | brightness, percentage, raw PWM, timed fade |

## Design rules

* **Opaque handles**: `hal_xxx_open(&h, ...)` / `hal_xxx_close(h)`. Callers
  never see fds or kernel structs. Each module exposes its fd only where an
  event loop needs it (`hal_gpio_fd`, `hal_uart_fd`).
* **One error convention**: `0`/byte count on success, `-errno` on failure.
  `hal_strerror()` turns the code into text. `errno` is never part of the API.
* **No hangs**: every blocking call takes `timeout_ms` (`-1` waits forever)
  and returns `-ETIMEDOUT`. `EINTR` is handled inside the library.
* **Thread safety**: handles can be shared between threads. I2C puts the
  slave address in every message (no `I2C_SLAVE` state). SPI, GPIO, PWM and
  backlight lock around read-modify-write sequences. UART has separate RX and
  TX locks, so a reader never blocks a writer. Closing a handle while another
  thread is still using it is the caller's bug.
* **Every fd is `O_CLOEXEC`**, so fds don't leak into child processes.
* **Pluggable OS layer**: all system calls go through `hal_sys_ops_t`.
  `hal_set_sys_ops()` swaps in a simulator, and `hal_set_sysfs_root()` moves
  `/sys` to a scratch tree. That is how the unit tests run on a build host
  with no hardware. A HIL rig can use the same hooks for fault injection.
* **Logging**: messages go to stderr at `warn` level by default. Change the
  level with `DEVHAL_LOG=none|error|warn|info|debug` or
  `hal_set_log_level()`. Route messages to syslog or your own logger with
  `hal_set_log_handler()`.

## Examples

```c
#include <devhal/devhal.h>

/* Read a 16-bit temperature register over I2C, with a busy-retry. */
hal_i2c_t *bus;
uint16_t raw;
if (hal_i2c_open(&bus, 1) == 0) {
    hal_i2c_set_retries(bus, 3, 500);
    if (hal_i2c_reg_read16be(bus, 0x48, 0x00, &raw) == 0)
        printf("T = %.2f C\n", (int16_t)raw / 256.0);
    hal_i2c_close(bus);
}

/* Debounced button with a timeout. */
hal_gpio_config_t cfg = HAL_GPIO_CONFIG_INIT;
cfg.edge = HAL_GPIO_EDGE_FALLING;
cfg.bias = HAL_GPIO_BIAS_PULL_UP;
cfg.debounce_us = 10000;
hal_gpio_t *btn;
hal_gpio_event_t ev;
hal_gpio_open(&btn, "gpiochip0", 23, &cfg);
if (hal_gpio_wait_event(btn, 5000, &ev) == -ETIMEDOUT)
    puts("no press within 5 s");

/* AT command over a modem UART. */
hal_uart_t *modem;
char line[128];
hal_uart_open(&modem, "/dev/ttyUSB2", NULL);        /* 115200 8N1 */
hal_uart_write(modem, "AT+CSQ\r", 7, 500);
while (hal_uart_read_line(modem, line, sizeof line, '\n', 1000) > 0)
    fputs(line, stdout);

/* Dim the LCD over 300 ms. */
hal_backlight_t *bl;
hal_backlight_open(&bl, NULL);
hal_backlight_fade(bl, 20, 300, 0);
```

### Reusing portable chip drivers

Chip drivers that take a write callback, such as [`../ltc3562`](../ltc3562),
plug straight into `hal_i2c` and get its retries and logging:

```c
ltc3562_init(&pmic, hal_i2c_write_cb, bus, 0);
```

`examples/pmic_ltc3562.c` is a complete example.

### `devhalctl`

`devhalctl` is a bring-up tool built on the library:

```
devhalctl i2c 1 scan
devhalctl i2c 1 get 0x48 0x00
devhalctl gpio gpiochip0 17 set 1
devhalctl gpio gpiochip0 23 watch 10 5000
devhalctl spi 0 0 xfer 0x9f 0 0 0
devhalctl uart /dev/ttyUSB2 115200 send AT+CGMR
devhalctl pwm 0 0 set 1000000 250000
devhalctl bl fade 255 500
```

## Build, test, install

```
make            # build/libdevhal.a, libdevhal.so.1.0.0, tests, tools
make test       # host unit tests (no hardware needed)
make asan       # the same tests under AddressSanitizer + UBSan
make install PREFIX=/usr DESTDIR=$PWD/stage   # headers, libs, devhal.pc
```

When cross-compiling, override `CC` (for example
`make CC=aarch64-linux-gnu-gcc`). The build needs kernel uAPI headers with
GPIO v2 (Linux 5.10 or later). `BL_UAPI` points at the backlight driver's
uAPI header, so the library and kernel module share one copy of it.

Link with `pkg-config --cflags --libs devhal` or `-ldevhal -lpthread`.

## Layout

```
include/devhal/   public headers (devhal.h includes all of them)
src/              implementation; hal_internal.h is private
tests/            mock_sys.c simulates the kernel drivers; test_devhal.c
examples/         devhalctl CLI, LTC3562 PMIC bring-up
```

## Tests

`tests/mock_sys.c` simulates the drivers behind `hal_sys_ops_t`:

* an I2C bus with a 256-byte register device at 0x50
* SPI loopback
* a GPIO chip with an injectable edge-event queue
* a UART with short writes and a TX-full mode
* a model of `pwm_lcd_bl`

The PWM tests use a temporary `/sys/class/pwm` tree. A helper thread plays
the kernel and creates the channel directory after `export` is written.
The tests also cover the error paths: NAKs, retry exhaustion, timeouts,
invalid configurations, buffer overflow and fd leaks.
