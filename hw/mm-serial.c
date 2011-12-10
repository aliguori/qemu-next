/*
 * QEMU 16550A UART emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2008 Citrix Systems, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "mm-serial.h"

/**
 * This demonstrates inheritence as a way to change the behavior of a device.
 */
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

    vmstate_register(NULL, s->base, &vmstate_serial, SERIAL_DEVICE(s));

    memory_region_init_io(&s->io, &serial_mm_ops[s->end], s,
                          TYPE_MM_SERIAL_DEVICE, 8 << s->it_shift);
    memory_region_add_subregion(s->address_space, s->base, &s->io);

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
