#ifndef DEVICE_H
#define DEVICE_H

#include "plug.h"

#include "qapi/qapi-visit-core.h"

typedef struct Device
{
    Plug parent;
} Device;

typedef struct DeviceClass
{
    PlugClass parent_class;

    /* public */
    void (*visit)(Device *device, Visitor *v, const char *name, Error **errp);
} DeviceClass;

#define TYPE_DEVICE "device"
#define DEVICE(obj) TYPE_CHECK(Device, obj, TYPE_DEVICE)
#define DEVICE_CLASS(class) TYPE_CLASS_CHECK(DeviceClass, class, TYPE_DEVICE)

void device_initialize(Device *device, const char *id);
void device_finalize(Device *device);

#endif
