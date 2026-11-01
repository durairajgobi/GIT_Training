# Personal Projects — Linux Device Drivers

Personal projects in Linux kernel and device driver development. All code is
in this repository.

---

## 1. PWM LCD Backlight Kernel Module — [`pwm_lcd_backlight/`](pwm_lcd_backlight/)

**Stack:** C, Linux kernel 6.x, platform driver, PWM and GPIO consumer APIs,
Device Tree, misc device, sysfs, ioctl

- Wrote a Device Tree–matched platform driver (`training,pwm-lcd-backlight`)
  that drives an LCD backlight from a PWM channel with an optional enable GPIO.
- Gave user space two ways to control it: ioctls on `/dev/lcd_backlight`
  (shared UAPI header) and sysfs attributes for enable, brightness, period,
  duty cycle and polarity.
- Applied each PWM change atomically with `pwm_apply_might_sleep()`. If the
  controller rejects a setting, the driver rolls back so the reported state
  always matches the hardware.
- Added power sequencing (PWM starts before the GPIO goes high, and stops after
  it goes low), suspend/resume/shutdown handling, and mutex-serialised updates.
- Made unbinding safe: open file descriptors hold a reference to the driver's
  data, so ioctls return `-ENODEV` instead of using freed memory.
- Wrote a `bl_ctl` command-line tool and an example DT overlay. The module
  cross-compiles for ARM64 and is compile-tested on 6.8 and 7.0 kernels.

## 2. LTC3562 I2C Quad Buck Regulator Driver — [`ltc3562/`](ltc3562/)

**Stack:** C99, I2C, Linux `i2c-dev`, mock-bus unit testing, Make

- Wrote a portable driver for the LTC3562 PMIC (2 × 600 mA + 2 × 400 mA buck
  rails). It sets output voltage, operating mode (pulse-skip, LDO, Burst,
  forced Burst) and enable per rail.
- Handled both rail types: Type A through feedback-divider maths (FB voltage
  425–800 mV), and Type B as a direct output voltage (600–3775 mV), in
  25 mV steps with rounding and range checks.
- The chip is write-only, so the driver keeps a shadow copy of each rail's
  state and updates it only after a write succeeds. It also supports
  broadcast shutdown.
- Kept the driver platform-independent behind a single I2C-write callback.
  Included a Linux `/dev/i2c-N` CLI and host unit tests against a mock I2C bus.

## 3. Linux Device Driver Sample Collection (100 modules) — [`linux_device_drivers/`](linux_device_drivers/)

**Stack:** C, Linux kernel 6.x, Kbuild, user-space test programs

- Wrote 100 self-contained kernel modules in 10 categories, building
  warning-free against Linux 6.8 headers. `LINUX_VERSION_CODE` guards cover
  API changes in later 6.x kernels.
- **Character devices and user-space interfaces:** cdev and misc devices,
  ioctl, `poll`/`select`, `fasync`, `mmap` (page and vmalloc), kfifo, procfs,
  seq_file, sysfs, debugfs.
- **Concurrency and deferred work:** mutex, spinlock, rwlock, RCU, seqlock,
  per-CPU data, completions, wait queues, timers, hrtimers, tasklets,
  workqueues, kthreads.
- **Hardware and buses:** shared and threaded IRQs, GPIO consumer and
  `gpio_chip` provider, DMA (coherent and streaming), MMIO, platform/DT,
  I2C, SPI, USB, PCI (BAR mapping, MSI), input and LED class drivers.
- **Subsystems:** net_device, netfilter, blk-mq block device, hwmon,
  watchdog, RTC, power_supply, kprobes, netlink.
- Wrote user-space test programs for the ioctl, poll, fasync, mmap and
  netlink samples.

---

## Skills shown

| Area | Topics |
|------|--------|
| Kernel frameworks | Platform bus, Device Tree, PWM, GPIO, I2C, SPI, USB, PCI, input, LED, hwmon, watchdog, RTC, power_supply, netdev, blk-mq |
| User-space interfaces | Character and misc devices, ioctl/UAPI, sysfs, procfs, debugfs, mmap, poll, netlink |
| Robustness | Locking, power management, hot-unbind safety, atomic hardware updates with rollback, devm resource management |
| Tooling | Kbuild out-of-tree builds, ARM64 cross-compilation, mock-hardware unit tests |

## Résumé version (short)

- **PWM LCD backlight driver (Linux kernel):** DT platform driver with PWM and
  GPIO power sequencing, ioctl and sysfs control, suspend/resume, and safe
  hot-unbind. Includes a user-space CLI.
- **LTC3562 PMIC driver (I2C):** portable C99 driver that sets voltage and
  mode on four buck rails, with shadow state for a write-only chip. Includes
  a Linux i2c-dev CLI and mock-bus unit tests.
- **Linux driver sample suite:** 100 kernel modules covering char devices,
  synchronization, IRQs, DMA, bus drivers (I2C/SPI/USB/PCI) and subsystems
  (netdev, blk-mq, hwmon, watchdog).
