CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS += -Iinclude

BUILD := build

all: $(BUILD)/test_ltc3562 $(BUILD)/ltc3562_linux

$(BUILD):
	mkdir -p $@

$(BUILD)/test_ltc3562: tests/test_ltc3562.c src/ltc3562.c include/ltc3562.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_ltc3562.c src/ltc3562.c

$(BUILD)/ltc3562_linux: examples/ltc3562_linux.c src/ltc3562.c include/ltc3562.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ examples/ltc3562_linux.c src/ltc3562.c

test: $(BUILD)/test_ltc3562
	./$(BUILD)/test_ltc3562

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
