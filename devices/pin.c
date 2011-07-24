#include "qemu/pin.h"

void pin_initialize(Pin *pin, const char *id)
{
    type_initialize(pin, TYPE_PIN, id);
}

void pin_finalize(Pin *pin)
{
    type_finalize(pin);
}

void pin_visit(Pin *obj, Visitor *v, const char *name, Error **errp)
{
    visit_start_struct(v, (void **)&obj, "Pin", name, sizeof(Pin), errp);
    device_visit(DEVICE(obj), v, "super", errp);
    visit_type_bool(v, &obj->level, "level", errp);
    visit_end_struct(v, errp);
}

void pin_set_level(Pin *pin, bool value, Error **errp)
{
    bool old_level = pin->level;

    pin->level = value;

    if (old_level != value) {
        notifier_list_notify(&pin->level_changed, NULL);
    }
}

bool pin_get_level(Pin *pin, Error **errp)
{
    return pin->level;
}

static void pin_initfn(TypeInstance *inst)
{
    Pin *obj = PIN(inst);

    notifier_list_init(&obj->level_changed);

    plug_add_property_bool(PLUG(obj), "level",
                           (PlugPropertyGetterBool *)pin_get_level, 
                           (PlugPropertySetterBool *)pin_set_level,
                           PROP_F_READWRITE);
}

static void pin_class_initfn(TypeClass *class)
{
    DeviceClass *device_class = DEVICE_CLASS(class);

    device_class->visit = (DeviceVisitor *)pin_visit;
}

static const TypeInfo pin_type_info = {
    .name = TYPE_PIN,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Pin),
    .instance_init = pin_initfn,
    .class_init = pin_class_initfn,
};

static void register_devices(void)
{
    type_register_static(&pin_type_info);
}

device_init(register_devices);
