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
#ifndef MM_SERIAL_H
#define MM_SERIAL_H

#include "serial.h"
#include "memory.h"

#define TYPE_MM_SERIAL_DEVICE "mm-serial-device"
#define MM_SERIAL_DEVICE(obj) \
     OBJECT_CHECK(MMSerialDevice, (obj), TYPE_MM_SERIAL_DEVICE)
#define MM_SERIAL_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(MMSerialDeviceClass, (klass), TYPE_MM_SERIAL_DEVICE)
#define MM_SERIAL_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(MMSerialDeviceClass, (obj), TYPE_MM_SERIAL_DEVICE)

typedef struct MMSerialDeviceClass
{
    SerialDeviceClass parent_class;
    int (*super_init)(DeviceState *dev);
} MMSerialDeviceClass;

typedef struct MMSerialDevice
{
    SerialDevice parent;
    enum device_endian end;
    int it_shift;
    target_phys_addr_t base;
    MemoryRegion *address_space;
    MemoryRegion io;
} MMSerialDevice;

MMSerialDevice *serial_mm_init(MemoryRegion *address_space,
                               target_phys_addr_t base, int it_shift,
                               qemu_irq irq, int baudbase,
                               CharDriverState *chr, enum device_endian end);

#endif
