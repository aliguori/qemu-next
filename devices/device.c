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

#if 0
static void device_visit_properties(Device *device, const char *name,
                                    Visitor *v, Error **errp)
{
}
#endif

static void device_unrealize(Plug *plug)
{
#if 0
    Device *device = DEVICE(plug);
    const char *typename, *id;
    QmpOutputVisitor *qov;
    QmpInputVisitor *qiv;

    /* 0) Save off id and type name

       1) Save each property.  We need to walk through each property, and for
          plug properties, recursively record the properties into a data
          structure.

       2) Call type_finalize(device);

       3) Call type_initialize(device, typename, id);

       4) Set properties, recursively of course to match (1)

       At the end of this function, the effective is that the device has been
       destroyed, recreated, with its properties restored.
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
