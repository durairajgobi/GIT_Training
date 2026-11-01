# ILI9341 SPI TFT Display Driver (DRM/KMS)

A Linux DRM/KMS driver for 240×320 ILI9341 TFT panels on a 4-wire SPI bus.
The panel shows up as a normal display: `/dev/dri/cardN` for KMS
applications, plus `/dev/fbN` through fbdev emulation for the console and
simple programs.

## Layout

| Path | Purpose |
|------|---------|
| `ili9341_drm.c` | Kernel module: SPI driver and DRM pipeline |
| `app/disp_test.c` | Test-pattern tool that uses the raw DRM UAPI (no libdrm needed) |
| `dts/ili9341-drm-overlay.dts` | Example Raspberry Pi overlay |
| `Makefile` | Builds the module, the tool and the overlay (out-of-tree) |

## How it works

```
 user space            DRM core                     ili9341_drm.c                  panel
 ----------   ----------------------------   -----------------------------   ---------------
 KMS app  --> atomic commit                   plane: damage rects ----------> CASET/PASET
 /dev/fbN     plane -> CRTC -> encoder ->     XRGB8888/RGB565 -> RGB565BE     RAMWR + pixels
              connector (SPI, fixed mode)     CRTC enable: reset + init  ---> SLPOUT, DISPON
              GEM shmem buffers               CRTC disable               ---> DISPOFF, SLPIN
```

The ILI9341 has its own frame memory (GRAM) and refreshes the glass from it,
so there is no scan-out engine to program. Framebuffers stay in system memory
(GEM shmem). On each commit the driver:

1. merges the damage clips into one rectangle (`drm_atomic_helper_damage_merged`),
2. converts that rectangle to big-endian RGB565 in a DMA-safe buffer,
3. sets the GRAM window with `CASET`/`PASET` and sends the pixels after
   `RAMWR`, split into chunks no larger than the SPI controller's limit.

When the CRTC is enabled, the driver resets and initialises the panel, writes
the whole frame, and only then turns on the display and backlight, so no
garbage appears on screen.

### Features

- **Formats:** XRGB8888 and RGB565, linear layout
- **Partial updates:** only the damaged area goes over SPI (`DIRTYFB` and
  atomic `FB_DAMAGE_CLIPS`)
- **Rotation:** 0/90/180/270 through `MADCTL`. At 90 and 270 the mode becomes 320×240.
- **Optional resources:** reset GPIO (otherwise a software reset is used), a
  `power-supply` regulator, a `backlight` phandle
- **Power management:** system suspend/resume through
  `drm_mode_config_helper_suspend/resume`. The panel turns off on shutdown.
- **Hot-unbind safety:** hardware access is wrapped in
  `drm_dev_enter()/drm_dev_exit()`, and `remove()` calls `drm_dev_unplug()`.
  Programs that still have the device open get errors instead of touching a
  device that is gone.
- **Locking:** a mutex keeps each command and its parameters/pixels together
  on the bus

## Device tree binding

```dts
display@0 {
	compatible = "training,ili9341-drm";
	reg = <0>;                                   /* chip select */
	spi-max-frequency = <32000000>;
	dc-gpios = <&gpio 24 GPIO_ACTIVE_HIGH>;      /* required */
	reset-gpios = <&gpio 25 GPIO_ACTIVE_LOW>;    /* optional */
	power-supply = <&vdd_3v3>;                   /* optional */
	backlight = <&backlight>;                    /* optional */
	rotation = <90>;                             /* optional: 0, 90, 180, 270 */
	training,rgb-panel;                          /* optional: RGB filter (default BGR) */
};
```

The `backlight` phandle must point to a backlight class device, such as
`gpio-backlight` or `pwm-backlight`. (The `pwm_lcd_backlight` module in this
repository is a misc device, not a backlight class device, so it can't be
linked here.)

## Build

```sh
make                                   # module + tool, for the running kernel
make KDIR=/path/to/kernel/build        # another kernel
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/path/to/arm64/kernel
make dtbo                              # ili9341-drm.dtbo from the overlay
```

The kernel needs `CONFIG_DRM`, `CONFIG_DRM_KMS_HELPER`,
`CONFIG_DRM_GEM_SHMEM_HELPER` and an SPI controller driver.
`CONFIG_DRM_FBDEV_EMULATION` is needed for `/dev/fbN`. The module builds with
no warnings (`W=1`) against Linux 6.8 and 7.0 headers. Version checks cover
the fbdev setup API (changed in 6.11 and 6.13) and the removal of
`drm_driver.date` (6.14).

## Load and test (Raspberry Pi example)

```sh
make dtbo && sudo cp ili9341-drm.dtbo /boot/firmware/overlays/
echo "dtoverlay=ili9341-drm" | sudo tee -a /boot/firmware/config.txt
sudo reboot

sudo insmod ili9341_drm.ko
dmesg | tail          # "320x240 ILI9341 on spi0.0, rotation 90, 32000000 Hz SPI"
ls /dev/dri /dev/fb*
```

Run these from a text console, with no X or Wayland session holding the
display:

```sh
sudo ./app/disp_test info
sudo ./app/disp_test bars 5            # colour bars (check R/G/B order)
sudo ./app/disp_test grid              # red border + green top-left corner
sudo ./app/disp_test gradient          # smooth ramps = RGB565 path OK
sudo ./app/disp_test fill ff8000
sudo ./app/disp_test bounce 10         # partial updates, prints fps
```

The console can also be shown on the panel through fbdev emulation:
`con2fbmap 1 1`, or boot with `fbcon=map:1`.

### Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Red and blue swapped | Panel has an RGB filter: add `training,rgb-panel` |
| Image mirrored or rotated | Change `rotation`, then use `disp_test grid` to check |
| White screen, no image | D/C or reset GPIO wrong, or the SPI clock is too fast: try 10 MHz |
| `SETCRTC` fails with `EACCES`/`EBUSY` | Another program (X, Wayland, Plymouth) is DRM master |

## Performance

A full 240×320 frame is 153,600 bytes. At 32 MHz SPI that is about 38 ms,
so full-screen updates top out near 25 fps. Small partial updates (such as
`bounce`) run much faster because only the changed rectangle is sent.
