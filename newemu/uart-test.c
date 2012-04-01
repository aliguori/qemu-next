#include <glib.h>
#include <stdio.h>

#include "newemu/uart.h"
#include "newemu/clock.h"
#include "newemu/serial_iface.h"

static int test_send(struct serial_interface *sif, uint8_t value)
{
    putchar(value);
    return 1;
}

struct serial_interface_ops sif_ops = {
    .send = test_send,
};

int main(int argc, char **argv)
{
    struct clock *clock = clock_get_instance();
    struct serial_interface sif = { .ops = &sif_ops };
    struct uart s;

    uart_init(&s, clock, &sif);

    uart_reset(&s);

    uart_cleanup(&s);

    return 0;
}
