CC ?= $(CROSS_COMPILE)gcc
CFLAGS += -Wall -Wextra -I../ta/include
LDLIBS += -lteec

tcu-keystore: tcu_keystore.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f tcu-keystore
