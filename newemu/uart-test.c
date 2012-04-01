#include <glib.h>
#include <stdio.h>
#include <unistd.h>

#include "newemu/uart.h"
#include "newemu/clock.h"
#include "newemu/serial_iface.h"

static int test_send(struct serial_interface *sif, uint8_t value)
{
    static int counter;

    if ((counter++ % 3) == 0) {
        putchar(value);
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

int main(int argc, char **argv)
{
    struct clock *clock = clock_get_instance();
    struct serial_interface sif = { .ops = &sif_ops };
    struct uart s;

    uart_init(&s, clock, &sif);

    uart_puts(&s, "Hello, world!\n");

    uart_io_write(&s, 1, UART_IER_THRI);

    uart_puts_int(&s, "Hello, world!\n");

    uart_cleanup(&s);

    return 0;
}
