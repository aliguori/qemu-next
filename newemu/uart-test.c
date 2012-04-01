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
    for (; *str; str++) {
        uart_putchar(s, *str);
    }
}

int main(int argc, char **argv)
{
    struct clock *clock = clock_get_instance();
    struct serial_interface sif = { .ops = &sif_ops };
    struct uart s;

    uart_init(&s, clock, &sif);

    uart_reset(&s);

    uart_puts(&s, "Hello, world!\n");

    /* FIXME */
    usleep(100000);

    uart_cleanup(&s);

    return 0;
}
