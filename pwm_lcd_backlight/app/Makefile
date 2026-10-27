CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
override CFLAGS += -I../include/uapi

bl_ctl: bl_ctl.c ../include/uapi/pwm_lcd_bl.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f bl_ctl

.PHONY: clean
