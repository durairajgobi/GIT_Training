# Builds every .c file in this directory as a separate kernel module.
obj-m := $(patsubst %.c,%.o,$(notdir $(filter-out %.mod.c,$(wildcard $(src)/*.c))))

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
