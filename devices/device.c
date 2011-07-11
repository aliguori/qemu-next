#include "qemu/device.h"
#include "qapi/qmp-output-visitor.h"

static void device_state_accessor(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    DeviceClass *class = DEVICE_GET_CLASS(DEVICE(plug));
    return class->visit(DEVICE(plug), v, name, errp);
}

static void device_initfn(TypeInstance *obj)
{
    Device *device = DEVICE(obj);

    plug_add_property_full(PLUG(device), "state",
                           device_state_accessor, NULL,
                           device_state_accessor, NULL,
                           type_get_type(obj),
                           PROP_F_READWRITE);
}

void device_visit(Device *device, Visitor *v, const char *name, Error **errp)
{
    visit_start_struct(v, (void **)&device, "Device", name, 0, errp);
    visit_end_struct(v, errp);
}

static void device_unrealize(Plug *plug)
{
#if 0
    Device *device = DEVICE(plug);
    QmpOutputVisitor *qov;
    Visitor *v;

    /* 0) Save off id and type name

       1) Save each property, we'll have to use a list of visitors to do this
       which sort of sucks.

       2) Call device_unrealize on all child plugs

       (sidebar) We need to not explicitly finalize plugs in each types finalize
       function.  This implies that we need a base class finalize to do this
       like Plug.  We need some magic so we can suppress that.

       3) Remove all properties.  This will inhibit plugs from being finalized
       automatically.

       4) Call type_finalize() on device.

       5) Call type_initialize() on device.

       6) For each saved property, set the property to the saved value.  The
       problem with this is that in (2) the properties would have been saved
       for child plugs.  But in (5) we'll reinitialize each plug (even though
       it's already been initialized.

       This process needs more thought.
    */
#endif
}

static void device_class_initfn(TypeClass *type_class)
{
    PlugClass *plug_class = PLUG_CLASS(type_class);
    DeviceClass *class = DEVICE_CLASS(type_class);

    plug_class->unrealize = device_unrealize;
    class->visit = device_visit;
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
