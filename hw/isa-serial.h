#ifndef ISA_SERIAL_H
#define ISA_SERIAL_H

#include "isa.h"
#include "serial.h"

#define TYPE_ISA_SERIAL_DEVICE "isa-serial"
#define ISA_SERIAL_DEVICE(obj) \
     OBJECT_CHECK(ISASerialDevice, (obj), TYPE_ISA_SERIAL_DEVICE)

typedef struct ISASerialDevice {
    ISADevice dev;
    uint32_t index;
    uint32_t iobase;
    uint32_t isairq;
    SerialDevice uart;
    CharDriverState *chr;
} ISASerialDevice;

DeviceState *serial_isa_init(int index, CharDriverState *chr);

#endif
