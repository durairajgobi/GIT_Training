# PWM LCD Backlight Kernel Module

A Linux platform driver that drives an LCD backlight from a PWM channel and
lets user space **enable**, **disable** and **configure** it through:

- a character device, `/dev/lcd_backlight`, which takes ioctls
- sysfs attributes in `/sys/class/misc/lcd_backlight/`

## Layout

| Path | Purpose |
|------|---------|
| `pwm_lcd_bl.c` | Kernel module (platform driver, misc device, sysfs) |
| `include/uapi/pwm_lcd_bl.h` | ioctl definitions shared with user space |
| `app/bl_ctl.c` | Command-line tool for user space |
| `dts/pwm-lcd-backlight-overlay.dts` | Example device-tree overlay |
| `Makefile` | Builds the module and the tool (out-of-tree) |

## Device tree binding

```dts
lcd-backlight {
	compatible = "training,pwm-lcd-backlight";
	pwms = <&pwm 0 50000 0>;            /* channel, period ns, flags */
	enable-gpios = <&gpio 18 GPIO_ACTIVE_HIGH>;  /* optional */
	max-brightness = <255>;             /* optional, default 255 */
	default-brightness = <128>;         /* optional, default max */
};
```

If the `pwms` property has no period, the driver uses the
`default_period_ns` module parameter (default 50000 ns, which is 20 kHz).
The backlight starts **off**. User space turns it on when it is ready.

## Build

```sh
make                                   # module + tool, for the running kernel
make KDIR=/path/to/kernel/build        # another kernel
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/path/to/arm64/kernel
```

The kernel needs `CONFIG_PWM` and a driver for your PWM controller. The
module builds on 6.x kernels. It has been compile-tested against 6.8 and 7.0.

## Load

```sh
sudo dtoverlay pwm-lcd-backlight.dtbo   # or add the node to the board DTS
sudo insmod pwm_lcd_bl.ko
dmesg | tail                           # "registered /dev/lcd_backlight ..."
```

## User-space interfaces

### sysfs

```sh
cd /sys/class/misc/lcd_backlight
echo 1 > enable              # enable
echo 0 > enable              # disable
echo 200 > brightness        # 0..max_brightness
echo 100000 > period_ns      # change period (brightness ratio is kept)
echo 25000 > duty_ns         # raw duty cycle (must be <= period_ns)
echo inversed > polarity     # normal | inversed
cat max_brightness
```

### ioctl (`include/uapi/pwm_lcd_bl.h`)

| ioctl | Argument | Effect |
|-------|----------|--------|
| `LCD_BL_IOC_ENABLE` | none | Turns the backlight on |
| `LCD_BL_IOC_DISABLE` | none | Turns the backlight off (the configuration is kept) |
| `LCD_BL_IOC_SET_CONFIG` | `struct lcd_bl_config *` | Sets period, duty and polarity |
| `LCD_BL_IOC_GET_CONFIG` | `struct lcd_bl_config *` | Reads the configuration, on/off state and brightness |
| `LCD_BL_IOC_SET_BRIGHTNESS` | `__u32 *` | Sets brightness from 0 to `max_brightness` |

### `bl_ctl` tool

```sh
./app/bl_ctl enable
./app/bl_ctl brightness 128
./app/bl_ctl config 50000 40000 normal
./app/bl_ctl status
./app/bl_ctl disable
```

## Driver behaviour

- **Atomic updates:** each change goes through `pwm_apply_might_sleep()`
  (`pwm_apply_state()` on kernels before 6.8). If the PWM controller rejects
  a setting, the driver keeps the previous one and returns the error, so the
  reported state always matches the hardware.
- **Power sequencing:** when turning on, the PWM starts before the enable
  GPIO. When turning off, the GPIO goes low before the PWM stops.
- **Suspend and resume:** the backlight turns off on suspend. On resume it
  goes back to its previous state. It also turns off on shutdown and when the
  driver is removed.
- **Safe removal:** open file descriptors hold a reference to the driver's
  data. After the device is unbound, ioctls return `-ENODEV` instead of using
  memory that has been freed.
- **Locking:** a mutex serialises every hardware update from sysfs, ioctls and
  power management.
