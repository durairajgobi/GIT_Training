# Out-of-tree build for the PWM LCD backlight driver.
#
#   make                       # build against the running kernel
#   make KDIR=/path/to/linux   # build against another kernel tree
#   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=...   # cross build

obj-m := pwm_lcd_bl.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all: modules app

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

app:
	$(MAKE) -C app CC="$(CROSS_COMPILE)gcc"

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(MAKE) -C app clean

.PHONY: all modules app clean
