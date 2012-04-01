CC=gcc

CFLAGS := -g -Wall -O -Werror
CFLAGS += -MMD -MP
CFLAGS += -I.

CFLAGS += $(shell pkg-config --cflags glib-2.0)
LDLIBS += $(shell pkg-config --libs glib-2.0)

CFLAGS += $(shell pkg-config --cflags gthread-2.0)
LDLIBS += $(shell pkg-config --libs gthread-2.0)

all: uart-test

uart-test: uart-test.o clock-glib.o timer-glib.o pin.o uart.o device.o

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) -MF .$@.deps $<

clean:
	$(RM) *.o *~ .*.deps

-include .*.deps
