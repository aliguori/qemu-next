#include "device.h"

static void device_initfn(TypeInstance *obj)
{
    Device *device = DEVICE(obj);

    plug_add_property_bool(PLUG(device), "realized",
                           (bool (*)(Plug *))device_get_realized,
                           (void (*)(Plug *, bool))device_set_realized,
                           PROP_F_READWRITE);
}

static void _device_on_realize(Device *device)
{
    /* FIXME: do something useful like locking all sockets */
    /* this requires having r/w flags for properties and being
       able to toggle flags dynamically */
}

/* Default reset implement completely reinitializes the device */
static void _device_on_reset(Device *device)
{
    TypeInstance *ti = TYPE_INSTANCE(device);
    const char *typename = type_get_name(ti->class->type);
    const char *id = ti->id;

    type_finalize(TYPE_INSTANCE(device));
    type_initialize(device, typename, id);

    /* FIXME save off properties and restore properties 
       maybe we should tag properties as host visible or something.
    */
}

static void _device_visit(Device *device, Visitor *v, const char *name, Error **errp)
{
    visit_start_struct(v, (void **)&device, "Device", name, 0, errp);
    visit_type_bool(v, &device->realized, "realized", errp);
    visit_end_struct(v, errp);
}

static void device_class_initfn(TypeClass *type_class)
{
    DeviceClass *class = DEVICE_CLASS(type_class);

    class->on_realize = _device_on_realize;
    class->on_reset = _device_on_reset;
    class->visit = _device_visit;
}

void device_set_realized(Device *device, bool realized)
{
    DeviceClass *class = DEVICE_CLASS(TYPE_INSTANCE(device)->class);

    if (!device->realized && realized) {
        device->realized = true;
        if (class->on_realize) {
            class->on_realize(device);
        }
    } else if (device->realized && !realized) {
        device->realized = false;
        if (class->on_reset) {
            class->on_reset(device);
        }
    }
}

bool device_get_realized(Device *device)
{
    return device->realized;
}

void device_visit(Device *device, Visitor *v, const char *name, Error **errp)
{
    DeviceClass *class = DEVICE_CLASS(TYPE_INSTANCE(device)->class);
    return class->visit(device, v, name, errp);
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
