CC=gcc

CFLAGS := -g -Wall -O -Werror
CFLAGS += -MMD -MP
CFLAGS += -I.

CFLAGS += $(shell pkg-config --cflags glib-2.0)
LDLIBS += $(shell pkg-config --libs glib-2.0)

CFLAGS += $(shell pkg-config --cflags gthread-2.0)
LDLIBS += $(shell pkg-config --libs gthread-2.0)

OBJ = clock-glib.o timer-glib.o pin.o uart.o device.o i8254.o

all: uart-test i8254-test

uart-test: uart-test.o $(OBJ)

i8254-test: i8254-test.o $(OBJ)

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) -MF .$@.deps $<

clean:
	$(RM) *.o *~ .*.deps uart-test i8254-test

-include .*.deps
