#ifndef DEVICE_H
#define DEVICE_H

#include "qemu/plug.h"

typedef struct Device
{
    Plug parent;
} Device;

typedef void (DeviceVisitor)(Device *device, Visitor *v, const char *name, Error **errp);

typedef struct DeviceClass
{
    PlugClass parent_class;

    /* public */
    DeviceVisitor *visit;
} DeviceClass;

#define TYPE_DEVICE "device"
#define DEVICE(obj) TYPE_CHECK(Device, obj, TYPE_DEVICE)
#define DEVICE_CLASS(class) TYPE_CLASS_CHECK(DeviceClass, class, TYPE_DEVICE)
#define DEVICE_GET_CLASS(obj) TYPE_GET_CLASS(DeviceClass, obj, TYPE_DEVICE)

void device_visit(Device *device, Visitor *v, const char *name, Error **errp);

#endif
