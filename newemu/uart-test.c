#include "newemu/uart.h"
#include "newemu/clock.h"
#include "newemu/serial_iface.h"

#include <glib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

struct test_sif
{
    struct serial_interface iface;
    char buffer[1024];
    size_t index;
    size_t size;
};

static int test_send(struct serial_interface *sif, uint8_t value)
{
    static int counter;
    struct test_sif *tif = container_of(sif, struct test_sif, iface);

    if ((counter++ % 3) == 0) {
        g_assert(tif->index < (tif->size - 1));

        tif->buffer[tif->index++] = value;
        tif->buffer[tif->index] = 0;
        return 1;
    }

    return 0;
}

struct serial_interface_ops sif_ops = {
    .send = test_send,
};

static void uart_putchar(struct uart *s, int ch)
{
    int lsr;

    do {
        lsr = uart_io_read(s, 5);
    } while (!(lsr & UART_LSR_THRE));

    uart_io_write(s, 0, ch);
}

static void uart_puts(struct uart *s, const char *str)
{
    int lsr;

    for (; *str; str++) {
        uart_putchar(s, *str);
    }

    do {
        lsr = uart_io_read(s, 5);
    } while (!(lsr & UART_LSR_TEMT));
}

static void uart_puts_int(struct uart *s, const char *str)
{
    int lsr;

    uart_io_read(s, 2);

    for (; *str; str++) {
        g_assert(uart_io_read(s, 5) & UART_LSR_THRE);

        uart_io_write(s, 0, *str);

        do {
            while (!pin_get_level(&s->irq)) {
                usleep(1);
            }
        } while (!(uart_io_read(s, 2) & UART_IIR_THRI));
    }

    do {
        lsr = uart_io_read(s, 5);
    } while (!(lsr & UART_LSR_TEMT));
}

static void uart_test(void)
{
    struct clock *clock = clock_get_instance();
    struct test_sif tif = { .iface.ops = &sif_ops,
                            .size = 1024 };
    struct uart s;

    uart_init(&s, clock, &tif.iface);

    uart_puts(&s, "Hello, world!\n");
    g_assert_cmpint(tif.index, ==, strlen("Hello, world!\n"));
    g_assert_cmpstr(tif.buffer, ==, "Hello, world!\n");
    tif.index = 0;

    uart_cleanup(&s);
}

static void uart_int_test(void)
{
    struct clock *clock = clock_get_instance();
    struct test_sif tif = { .iface.ops = &sif_ops,
                            .size = 1024 };
    struct uart s;

    uart_init(&s, clock, &tif.iface);

    uart_io_write(&s, 1, UART_IER_THRI);

    uart_puts_int(&s, "Hello, world!\n");
    g_assert_cmpint(tif.index, ==, strlen("Hello, world!\n"));
    g_assert_cmpstr(tif.buffer, ==, "Hello, world!\n");
    tif.index = 0;

    uart_cleanup(&s);
}

int main(int argc, char **argv)
{
    /* gtester isn't getting along with our threading yet... */

    uart_test();
    uart_int_test();

    return 0;
}
