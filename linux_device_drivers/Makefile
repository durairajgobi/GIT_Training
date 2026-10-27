# Build all sample modules (and the user-space test apps).
#   make                 # against the running kernel
#   make KDIR=/path/to/kernel/build
SUBDIRS := $(sort $(wildcard [0-9][0-9]_*))
KDIR ?= /lib/modules/$(shell uname -r)/build

all: modules apps

modules:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d KDIR=$(KDIR) || exit 1; done

apps:
	$(MAKE) -C user_apps

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d KDIR=$(KDIR) clean; done
	$(MAKE) -C user_apps clean

.PHONY: all modules apps clean
