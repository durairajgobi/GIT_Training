# Out-of-tree build for the ILI9341 DRM display driver.
#
#   make                       # build against the running kernel
#   make KDIR=/path/to/linux   # build against another kernel tree
#   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=...   # cross build
#   make dtbo                  # compile the example DT overlay

obj-m := ili9341_drm.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all: modules app

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

app:
	$(MAKE) -C app CC="$(CROSS_COMPILE)gcc"

dtbo: dts/ili9341-drm-overlay.dts
	dtc -@ -I dts -O dtb -o ili9341-drm.dtbo $<

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(MAKE) -C app clean
	rm -f ili9341-drm.dtbo

.PHONY: all modules app dtbo clean
