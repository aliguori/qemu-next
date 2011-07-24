#include "qemu/device.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp-input-visitor.h"

static void device_state_accessor(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    DeviceClass *class = DEVICE_GET_CLASS(DEVICE(plug));
    return class->visit(DEVICE(plug), v, name, errp);
}

static void device_initfn(TypeInstance *obj)
{
    Device *device = DEVICE(obj);

    plug_add_property_full(PLUG(device), "state",
                           device_state_accessor,
                           device_state_accessor,
                           NULL,
                           NULL,
                           type_get_type(obj),
                           PROP_F_READWRITE);
}

void device_visit(Device *device, Visitor *v, const char *name, Error **errp)
{
    visit_start_struct(v, (void **)&device, "Device", name, 0, errp);
    visit_end_struct(v, errp);
}

static void device_visit_properties(Device *device, bool is_input, const char *name, Visitor *v, Error **errp);

typedef struct DeviceVisitPropertyData
{
    Visitor *v;
    Error **errp;
    bool is_input;
} DeviceVisitPropertyData;

static void device_visit_property(Plug *plug, const char *name, const char *typename, int flags, void *opaque)
{
    DeviceVisitPropertyData *data = opaque;

    if (strcmp(name, "state") == 0 || strcmp(name, "realized") == 0) {
        return;
    }

    if (strstart(typename, "plug<", NULL)) {
        Device *value = DEVICE(plug_get_property_plug(plug, NULL, name));
        device_visit_properties(value, data->is_input, name, data->v, data->errp);
    } else if (data->is_input) {
        if ((flags & PROP_F_READ) && (flags & PROP_F_WRITE)) {
            plug_set_property(plug, name, data->v, data->errp);
        }
    } else {
        if (flags & PROP_F_READ) {
            plug_get_property(plug, name, data->v, data->errp);
        }
    }
}

static void device_visit_properties(Device *device, bool is_input, const char *name, Visitor *v, Error **errp)
{
    DeviceVisitPropertyData data = {
        .v = v,
        .errp = errp,
        .is_input = is_input,
    };

    visit_start_struct(v, (void **)&device, "Device", name, sizeof(Device), errp);
    plug_foreach_property(PLUG(device), device_visit_property, &data);
    visit_end_struct(v, errp);
}

static void device_unrealize(Plug *plug)
{
    Device *device = DEVICE(plug);
    const char *typename;
    char id[MAX_ID];
    QmpOutputVisitor *qov;
    QmpInputVisitor *qiv;
    Error *local_err = NULL; // FIXME

    snprintf(id, sizeof(id), "%s", type_get_id(TYPE_INSTANCE(device)));
    typename = type_get_type(TYPE_INSTANCE(device));

    qov = qmp_output_visitor_new();

    device_visit_properties(device, false, id, qmp_output_get_visitor(qov), &local_err);

    type_finalize(device);
    type_initialize(device, typename, id);

    qiv = qmp_input_visitor_new(qmp_output_get_qobject(qov));

    device_visit_properties(device, true, id, qmp_input_get_visitor(qiv), &local_err);

    qmp_input_visitor_cleanup(qiv);
    qmp_output_visitor_cleanup(qov);
}

static void device_class_initfn(TypeClass *type_class)
{
    PlugClass *plug_class = PLUG_CLASS(type_class);
    DeviceClass *class = DEVICE_CLASS(type_class);

    plug_class->unrealize = device_unrealize;
    class->visit = device_visit;
}

static const TypeInfo device_type_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_PLUG,
    .instance_size = sizeof(Device),
    .class_size = sizeof(DeviceClass),
    .class_init = device_class_initfn,
    .instance_init = device_initfn,
    .abstract = true,
};

static void register_devices(void)
{
    type_register_static(&device_type_info);
}

device_init(register_devices);
