#ifndef MM_SERIAL_H
#define MM_SERIAL_H

#include "serial.h"

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
} MMSerialDevice;

MMSerialDevice *serial_mm_init(MemoryRegion *address_space,
                               target_phys_addr_t base, int it_shift,
                               qemu_irq irq, int baudbase,
                               CharDriverState *chr, enum device_endian end);

#endif
