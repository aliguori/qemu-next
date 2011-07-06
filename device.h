#ifndef DEVICE_H
#define DEVICE_H

#include "plug.h"

typedef struct Device
{
    Plug parent;

    int y;
} Device;

typedef struct DeviceClass
{
    PlugClass parent_class;

    void (*realize)(Device *device);
    void (*reset)(Device *device);
} DeviceClass;

#define TYPE_DEVICE "device"
#define DEVICE(obj) TYPE_CHECK(Device, obj, TYPE_DEVICE)
#define DEVICE_CLASS(class) TYPE_CLASS_CHECK(DeviceClass, class, TYPE_DEVICE)

void device_initialize(Device *device, const char *id);

void device_realize(Device *device);

void device_reset(Device *device);

#endif
