#include "device.h"

static void device_visit(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    DeviceClass *class = DEVICE_CLASS(TYPE_INSTANCE(plug)->class);
    // FIXME reset before set
    return class->visit(DEVICE(plug), v, name, errp);
}

static void device_initfn(TypeInstance *obj)
{
    Device *device = DEVICE(obj);

    plug_add_property_full(PLUG(device), "state",
                           device_visit, NULL,
                           device_visit, NULL,
                           type_get_type(TYPE_INSTANCE(device)),
                           PROP_F_READWRITE);
}

static void _device_visit(Device *device, Visitor *v, const char *name, Error **errp)
{
    visit_start_struct(v, (void **)&device, "Device", name, 0, errp);
    visit_end_struct(v, errp);
}

static void device_class_initfn(TypeClass *type_class)
{
    DeviceClass *class = DEVICE_CLASS(type_class);

    class->visit = _device_visit;
}

void device_initialize(Device *device, const char *id)
{
    type_initialize(device, TYPE_DEVICE, id);
}

void device_finalize(Device *device)
{
    type_finalize(device);
}

static const TypeInfo device_type_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_PLUG,
    .instance_size = sizeof(Device),
    .class_size = sizeof(DeviceClass),
    .class_init = device_class_initfn,
    .instance_init = device_initfn,
};

static void register_devices(void)
{
    type_register_static(&device_type_info);
}

device_init(register_devices);
