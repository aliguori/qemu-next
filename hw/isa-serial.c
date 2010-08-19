#include "serial.h"
#include "isa.h"

/* The ISA serial device is a very simple ISA device that contains a UART
 * 16650A chip.  This device merely bridges I/O to the UART chipset.
 */

typedef struct ISASerial {
    ISADevice dev;
    SerialBridgeState bridge;
    CharDriverState *chr;
    uint32_t iobase;
    uint32_t isairq;
} ISASerial;

static void isa_serial_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    ISASerialState *s = opaque;
    SerialState *uart = serial_bridge_get_uart(&s->bridge);

    serial_io_out(uart, addr, val);
}

static uint32_t isa_serial_ioport_read(void *opaque, uint32_t addr)
{
    ISASerialState *s = opaque;
    SerialState *uart = serial_bridge_get_uart(&s->bridge);

    return serial_io_in(uart, addr);
}

static void isa_serial_reset(ISADevice *isa_dev)
{
    ISASerialState *s = DO_UPCAST(ISASerialState, dev, isa_dev);

    serial_bridge_reset(&isa_dev.bridge);
}

static int serial_isa_initfn(ISADevice *isa_dev)
{
    ISASerialState *s = DO_UPCAST(ISASerialState, dev, isa_dev);
    DeviceState *uart;
    qemu_irq irq;

    /* create the uart bridge interface */
    serial_bridge_create_inplace(&isa_dev.bridge);

    /* create a uart chip connected off of our bridge */
    uart = qdev_create(&s->bridge->qbus, "UART-16650A");

    /* transfer the chr property directly to the uart chip */
    qdev_prop_set_chr(uart, "chr", isa_dev.chr);

    /* connect the IRQ */
    isa_init_irq(isa_dev, &irq, isa_dev->isairq);
    qdev_connect_gpio_out(uart, 0, irq);

    /* FIXME: this should be an ISA specific interface */
    register_ioport_write(isa_dev->iobase, 8, 1,
                          isa_serial_ioport_write, isa_dev);
    register_ioport_read(isa_dev->iobase, 8, 1,
                         isa_serial_ioport_read, isa_dev);

    /* init our child uart chip */
    qdev_init_nofail(uart);
}

static ISADeviceInfo serial_isa_info = {
    .qdev.name  = "isa-serial",
    .qdev.size  = sizeof(ISASerialState),
    .init       = serial_isa_initfn,
    .reset      = serial_isa_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_HEX32("iobase", ISASerialState, iobase,  -1),
        DEFINE_PROP_UINT32("irq",   ISASerialState, isairq,  -1),
        DEFINE_PROP_CHR("chardev",  ISASerialState, chr),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void isa_serial_register_devices(void)
{
    isa_qdev_register(&isa_serial_info);
}

device_init(isa_serial_register_devices)


