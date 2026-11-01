CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2
WARN    := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -Wmissing-prototypes -Werror
# pwm_lcd_bl.h is shared with the kernel driver; keep one copy of the uAPI.
BL_UAPI ?= ../pwm_lcd_backlight/include/uapi
CPPFLAGS += -Iinclude -Isrc -I$(BL_UAPI) -D_DEFAULT_SOURCE
LDLIBS  += -lpthread

PREFIX  ?= /usr/local
BUILD   := build
VERSION := 1.0.0
SONAME  := libdevhal.so.1

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=$(BUILD)/obj/%.o)
HDRS := $(wildcard include/devhal/*.h) src/hal_internal.h

LIB_A  := $(BUILD)/libdevhal.a
LIB_SO := $(BUILD)/libdevhal.so.$(VERSION)

TESTS := $(BUILD)/test_devhal
TOOLS := $(BUILD)/devhalctl $(BUILD)/pmic_ltc3562

all: $(LIB_A) $(LIB_SO) $(TESTS) $(TOOLS)

$(BUILD)/obj:
	mkdir -p $@

$(BUILD)/obj/%.o: src/%.c $(HDRS) | $(BUILD)/obj
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -fPIC -c -o $@ $<

$(LIB_A): $(OBJS)
	$(AR) rcs $@ $^

$(LIB_SO): $(OBJS)
	$(CC) -shared -Wl,-soname,$(SONAME) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	ln -sf libdevhal.so.$(VERSION) $(BUILD)/$(SONAME)
	ln -sf $(SONAME) $(BUILD)/libdevhal.so

$(BUILD)/test_devhal: tests/test_devhal.c tests/mock_sys.c tests/mock_sys.h $(LIB_A)
	$(CC) $(CPPFLAGS) -Itests $(WARN) $(CFLAGS) -o $@ \
		tests/test_devhal.c tests/mock_sys.c $(LIB_A) $(LDLIBS)

$(BUILD)/devhalctl: examples/devhalctl.c $(LIB_A)
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -o $@ $< $(LIB_A) $(LDLIBS)

# Reuses the portable LTC3562 driver from ../ltc3562 on top of hal_i2c.
LTC3562 ?= ../ltc3562
$(BUILD)/pmic_ltc3562: examples/pmic_ltc3562.c $(LTC3562)/src/ltc3562.c $(LIB_A)
	$(CC) $(CPPFLAGS) -I$(LTC3562)/include $(WARN) $(CFLAGS) -o $@ \
		$< $(LTC3562)/src/ltc3562.c $(LIB_A) $(LDLIBS)

test: $(TESTS)
	./$(BUILD)/test_devhal

# Same tests under AddressSanitizer + UBSan.
asan:
	$(MAKE) BUILD=build-asan CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address,undefined" LDLIBS="-lpthread -fsanitize=address,undefined" test

install: $(LIB_A) $(LIB_SO)
	install -d $(DESTDIR)$(PREFIX)/include/devhal $(DESTDIR)$(PREFIX)/lib/pkgconfig
	install -m 644 include/devhal/*.h $(DESTDIR)$(PREFIX)/include/devhal/
	install -m 644 $(LIB_A) $(DESTDIR)$(PREFIX)/lib/
	install -m 755 $(LIB_SO) $(DESTDIR)$(PREFIX)/lib/
	ln -sf libdevhal.so.$(VERSION) $(DESTDIR)$(PREFIX)/lib/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(PREFIX)/lib/libdevhal.so
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(VERSION)|' devhal.pc.in \
		> $(DESTDIR)$(PREFIX)/lib/pkgconfig/devhal.pc

clean:
	rm -rf build build-asan

.PHONY: all test asan install clean
