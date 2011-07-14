#include "qemu/rom.h"

void rom_device_initialize(RomDevice *obj, const char *id)
{
    type_initialize(obj, TYPE_ROM_DEVICE, id);
}

void rom_device_finalize(RomDevice *obj)
{
    type_finalize(obj);
}

void rom_device_visit(Device *device, Visitor *v, const char *name, Error **errp)
{
}

void rom_device_set_size(RomDevice *obj, uint32_t size)
{
    obj->capacity = size; // FIXME resize
}

uint32_t rom_device_get_size(RomDevice *obj)
{
    if (obj->capacity == 0) {
        return obj->size;
    }

    return obj->capacity;
}

void rom_device_set_filename(RomDevice *obj, const char *filename)
{
    qemu_free(obj->filename);
    obj->filename = qemu_strdup(filename);

    if (obj->capacity == 0) {
        /* use the file size */
    } else {
        /* fixed capacity */
    }
}

const char *rom_device_get_filename(RomDevice *obj, const char *filename)
{
    return obj->filename;
}

uint64_t rom_device_read(RomDevice *obj, uint32_t offset, int size)
{
    return 0;
}

static TypeInfo rom_type_info = {
    .name = TYPE_ROM_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(RomDevice),
};

static void register_devices(void)
{
    type_register_static(&rom_type_info);
}

device_init(register_devices);
