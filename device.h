#ifndef DEVICE_H
#define DEVICE_H

#include "plug.h"

#include "qapi/qapi-visit-core.h"

typedef struct Device
{
    Plug parent;

    bool realized;
} Device;

typedef struct DeviceClass
{
    PlugClass parent_class;

    /* public */
    void (*visit)(Device *device, Visitor *v, const char *name, Error **errp);

    /* protected */
    void (*on_realize)(Device *device);
    void (*on_reset)(Device *device);
} DeviceClass;

#define TYPE_DEVICE "device"
#define DEVICE(obj) TYPE_CHECK(Device, obj, TYPE_DEVICE)
#define DEVICE_CLASS(class) TYPE_CLASS_CHECK(DeviceClass, class, TYPE_DEVICE)

void device_initialize(Device *device, const char *id);
void device_finalize(Device *device);

void device_realize(Device *device);

void device_reset(Device *device);

void device_set_realized(Device *device, bool realized);

bool device_get_realized(Device *device);

void device_visit(Device *device, Visitor *v, const char *name, Error **errp);

#endif
