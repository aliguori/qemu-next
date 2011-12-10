#include "isa-serial.h"

static const int isa_serial_io[MAX_SERIAL_PORTS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static const int isa_serial_irq[MAX_SERIAL_PORTS] = { 4, 3, 4, 3 };

static const MemoryRegionPortio serial_portio[] = {
    { 0, 8, 1,
      .read = (IOPortReadFunc *)serial_ioport_read,
      .write = (IOPortWriteFunc *)serial_ioport_write },
    PORTIO_END_OF_LIST()
};

static const MemoryRegionOps serial_io_ops = {
    .old_portio = serial_portio
};

static int serial_isa_initfn(ISADevice *dev)
{
    static int index;
    ISASerialDevice *isa = DO_UPCAST(ISASerialDevice, dev, dev);
    SerialDevice *s = &isa->uart;
    int err;

    if (isa->index == -1)
        isa->index = index;
    if (isa->index >= MAX_SERIAL_PORTS)
        return -1;
    if (isa->iobase == -1)
        isa->iobase = isa_serial_io[isa->index];
    if (isa->isairq == -1)
        isa->isairq = isa_serial_irq[isa->index];
    index++;

    s->baudbase = 115200;
    s->chr = isa->chr;

    err = qdev_init(DEVICE(s));
    if (err < 0) {
        return err;
    }

    isa_init_irq(dev, &s->irq, isa->isairq);

    qdev_set_legacy_instance_id(&dev->qdev, isa->iobase, 3);

    memory_region_init_io(&s->io, &serial_io_ops, s, "serial", 8);
    isa_register_ioport(dev, &s->io, isa->iobase);
    return 0;
}

static const VMStateDescription vmstate_isa_serial = {
    .name = "serial",
    .version_id = 3,
    .minimum_version_id = 2,
    .fields      = (VMStateField []) {
        VMSTATE_STRUCT(uart, ISASerialDevice, 0, vmstate_serial, SerialDevice),
        VMSTATE_END_OF_LIST()
    }
};

static Property serial_isa_properties[] = {
    DEFINE_PROP_UINT32("index", ISASerialDevice, index,   -1),
    DEFINE_PROP_HEX32("iobase", ISASerialDevice, iobase,  -1),
    DEFINE_PROP_UINT32("irq",   ISASerialDevice, isairq,  -1),
    DEFINE_PROP_CHR("chardev",  ISASerialDevice, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void serial_isa_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISADeviceClass *ic = ISA_DEVICE_CLASS(klass);
    ic->init = serial_isa_initfn;
    dc->vmsd = &vmstate_isa_serial;
    dc->props = serial_isa_properties;
}

static void serial_isa_inst_initfn(Object *obj)
{
    ISASerialDevice *dev = ISA_SERIAL_DEVICE(obj);

    object_initialize(&dev->uart, TYPE_SERIAL_DEVICE);
    qdev_property_add_child(DEVICE(dev), "uart", DEVICE(&dev->uart), NULL);
    qdev_prop_set_globals(DEVICE(dev));
}

static TypeInfo serial_isa_info = {
    .name          = TYPE_ISA_SERIAL_DEVICE,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISASerialDevice),
    .class_init    = serial_isa_class_initfn,
    .instance_init = serial_isa_inst_initfn,
};

DeviceState *serial_isa_init(int index, CharDriverState *chr)
{
    ISADevice *dev;

    dev = isa_try_create(TYPE_ISA_SERIAL_DEVICE);
    if (!dev) {
        return NULL;
    }
    qdev_prop_set_uint32(&dev->qdev, "index", index);
    qdev_prop_set_chr(&dev->qdev, "chardev", chr);
    if (qdev_init(&dev->qdev) < 0) {
        return NULL;
    }
    return &dev->qdev;
}

static void serial_register_devices(void)
{
    type_register_static(&serial_isa_info);
}

device_init(serial_register_devices)
