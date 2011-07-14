#ifndef ROM_H
#define ROM_H

#include "device.h"

typedef struct RomDevice
{
    Device parent;

    char *filename;

    uint32_t capacity;
    uint32_t size;
    uint8_t *data;
} RomDevice;

#define TYPE_ROM_DEVICE "rom"
#define ROM_DEVICE(obj) TYPE_CHECK(RomDevice, obj, TYPE_ROM_DEVICE)

void rom_device_initialize(RomDevice *obj, const char *id);
void rom_device_finalize(RomDevice *obj);
void rom_device_visit(Device *device, Visitor *v, const char *name, Error **errp);

void rom_device_set_size(RomDevice *obj, uint32_t size);
uint32_t rom_device_get_size(RomDevice *obj);

void rom_device_set_filename(RomDevice *obj, const char *filename);
const char *rom_device_get_filename(RomDevice *obj, const char *filename);

uint64_t rom_device_read(RomDevice *obj, uint32_t offset, int size);

#endif
