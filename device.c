#include "device.h"

static void device_initfn(TypeInstance *obj)
{
    Device *device = DEVICE(obj);

    device->y = 84;
}

void device_realize(Device *device)
{
    DeviceClass *class = DEVICE_CLASS(TYPE_INSTANCE(device)->class);
    return class->realize(device);
}

void device_reset(Device *device)
{
    DeviceClass *class = DEVICE_CLASS(TYPE_INSTANCE(device)->class);
    return class->reset(device);
}

void device_initialize(Device *device)
{
    type_initialize(device, TYPE_DEVICE);
}

static const TypeInfo device_type_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_PLUG,
    .instance_size = sizeof(Device),
    .class_size = sizeof(DeviceClass),
    .instance_init = device_initfn,
};

static void register_devices(void)
{
    type_register_static(&device_type_info);
}

device_init(register_devices);
