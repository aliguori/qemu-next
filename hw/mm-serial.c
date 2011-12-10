#include "mm-serial.h"

static uint64_t serial_mm_read(void *opaque, target_phys_addr_t addr,
                               unsigned size)
{
    MMSerialDevice *s = opaque;
    return serial_ioport_read(SERIAL_DEVICE(s), addr >> s->it_shift);
}

static void serial_mm_write(void *opaque, target_phys_addr_t addr,
                            uint64_t value, unsigned size)
{
    MMSerialDevice *s = opaque;
    value &= ~0u >> (32 - (size * 8));
    serial_ioport_write(SERIAL_DEVICE(s), addr >> s->it_shift, value);
}

static const MemoryRegionOps serial_mm_ops[3] = {
    [DEVICE_NATIVE_ENDIAN] = {
        .read = serial_mm_read,
        .write = serial_mm_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
    },
    [DEVICE_LITTLE_ENDIAN] = {
        .read = serial_mm_read,
        .write = serial_mm_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
    },
    [DEVICE_BIG_ENDIAN] = {
        .read = serial_mm_read,
        .write = serial_mm_write,
        .endianness = DEVICE_BIG_ENDIAN,
    },
};

static int serial_mm_initfn(DeviceState *dev)
{
    MMSerialDevice *s = MM_SERIAL_DEVICE(dev);
    MMSerialDeviceClass *sc = MM_SERIAL_DEVICE_CLASS(object_get_super(OBJECT(dev)));
    int err;

    err = sc->super_init(dev);
    if (err < 0) {
        return err;
    }

    vmstate_register(NULL, s->base, &vmstate_serial, &s->parent);

    memory_region_init_io(&s->parent.io, &serial_mm_ops[s->end], s,
                          TYPE_MM_SERIAL_DEVICE, 8 << s->it_shift);
    memory_region_add_subregion(s->address_space, s->base, &s->parent.io);

    return 0;
}

static void serial_mm_class_init(ObjectClass *klass, void *data)
{
    MMSerialDeviceClass *s = MM_SERIAL_DEVICE_CLASS(klass);
    SerialDeviceClass *p = SERIAL_DEVICE_CLASS(klass);

    s->super_init = p->init;
    p->init = serial_mm_initfn;
}

static TypeInfo serial_mm_info = {
    .name = TYPE_MM_SERIAL_DEVICE,
    .parent = TYPE_SERIAL_DEVICE,
    .class_size = sizeof(MMSerialDeviceClass),
    .instance_size = sizeof(MMSerialDevice),
    .class_init = serial_mm_class_init,
};

MMSerialDevice *serial_mm_init(MemoryRegion *address_space,
                               target_phys_addr_t base, int it_shift,
                               qemu_irq irq, int baudbase,
                               CharDriverState *chr, enum device_endian end)
{
    MMSerialDevice *s;

    s = MM_SERIAL_DEVICE(object_new(TYPE_MM_SERIAL_DEVICE));

    s->base = base;
    s->it_shift = it_shift;
    s->address_space = address_space;

    s->parent.irq = irq;
    s->parent.baudbase = baudbase;
    s->parent.chr = chr;

    if (qdev_init(DEVICE(s)) < 0) {
        return NULL;
    }

    return s;
}

static void register_serial_devices(void)
{
    type_register_static(&serial_mm_info);
}

device_init(register_serial_devices);
