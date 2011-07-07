#include "gate.h"

void gate_initialize(Gate *gate, const char *id)
{
    type_initialize(gate, TYPE_GATE, id);
}

void gate_finalize(Gate *gate)
{
    type_finalize(gate);
}

void gate_set_level(Gate *gate, bool level)
{
    gate->level = level;
}

bool gate_get_level(Gate *gate)
{
    return gate->level;
}

static void gate_initfn(TypeInstance *inst)
{
    Gate *obj = GATE(inst);

    plug_add_property_bool(PLUG(obj), "level",
                           (bool (*)(Plug *))gate_get_level, 
                           (void (*)(Plug *, bool))gate_set_level,
                           PROP_F_READWRITE);
}

static const TypeInfo gate_type_info = {
    .name = TYPE_GATE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Gate),
    .instance_init = gate_initfn,
};

static void register_devices(void)
{
    type_register_static(&gate_type_info);
}

device_init(register_devices);
