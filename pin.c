#include "pin.h"

void pin_initialize(Pin *pin, const char *id)
{
    type_initialize(pin, TYPE_PIN, id);
}

void pin_finalize(Pin *pin)
{
    type_finalize(pin);
}

void pin_set_level(Pin *pin, bool value)
{
    bool old_level = pin->level;

    pin->level = value;

    if (old_level != value) {
        notifier_list_notify(&pin->level_changed);
    }
}

bool pin_get_level(Pin *pin)
{
    return pin->level;
}

static void pin_initfn(TypeInstance *inst)
{
    Pin *obj = PIN(inst);

    notifier_list_init(&obj->level_changed);

    plug_add_property_bool(PLUG(obj), "level",
                           (bool (*)(Plug *))pin_get_level, 
                           (void (*)(Plug *, bool))pin_set_level,
                           PROP_F_READWRITE);
}

static const TypeInfo pin_type_info = {
    .name = TYPE_PIN,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Pin),
    .instance_init = pin_initfn,
};

static void register_devices(void)
{
    type_register_static(&pin_type_info);
}

device_init(register_devices);
