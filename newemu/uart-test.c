#include <glib.h>

#include "newemu/uart.h"
#include "newemu/clock.h"
#include "newemu/serial_iface.h"

struct serial_interface_ops sif_ops = {
};

int main(int argc, char **argv)
{
    struct clock *clock = clock_get_instance();
    struct serial_interface sif = { .ops = &sif_ops };
    struct uart s;

    uart_init(&s, clock, &sif);

    uart_cleanup(&s);

    return 0;
}
