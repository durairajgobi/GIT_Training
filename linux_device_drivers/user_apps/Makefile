CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
APPS := $(patsubst %.c,%,$(wildcard *.c))

all: $(APPS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(APPS)
