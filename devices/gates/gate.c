#include "qemu/gate.h"

void gate_initialize(Gate *gate, const char *id)
{
    type_initialize(gate, TYPE_GATE, id);
}

void gate_finalize(Gate *gate)
{
    type_finalize(gate);
}

static bool gate_compute(Gate *obj, bool in0, bool in1)
{
    GateClass *class;

    class = GATE_CLASS(type_get_class(TYPE_INSTANCE(obj)));

    return class->compute(obj, in0, in1);
}

static void gate_on_level_changed(Gate *obj)
{
    bool level;

    level = gate_compute(obj, pin_get_level(obj->in[0]), pin_get_level(obj->in[1]));
    pin_set_level(&obj->out, level);
}

static void gate_on_in0_level_changed(Notifier *notifier)
{
    Gate *obj = container_of(notifier, Gate, in_level_changed[0]);

    gate_on_level_changed(obj);
}

static void gate_on_in1_level_changed(Notifier *notifier)
{
    Gate *obj = container_of(notifier, Gate, in_level_changed[1]);

    gate_on_level_changed(obj);
}

void gate_visit(Gate *obj, Visitor *v, const char *name, Error **errp)
{
    visit_start_struct(v, (void **)&obj, "Gate", name, sizeof(*obj), errp);
    device_visit(DEVICE(obj), v, "super", errp);
    pin_visit(&obj->out, v, "out", errp);
    visit_end_struct(v, errp);
}

static void gate_initfn(TypeInstance *inst)
{
    Gate *obj = GATE(inst);
    char name[256];

    snprintf(name, sizeof(name), "%s::out", type_get_id(inst));
    pin_initialize(&obj->out, name);

    obj->in_level_changed[0].notify = gate_on_in0_level_changed;
    obj->in_level_changed[1].notify = gate_on_in1_level_changed;

    plug_add_property_plug(PLUG(obj), "out", (Plug *)&obj->out, TYPE_PIN);
    plug_add_property_socket(PLUG(obj), "in[0]", (Plug **)&obj->in[0], TYPE_PIN);
    plug_add_property_socket(PLUG(obj), "in[1]", (Plug **)&obj->in[1], TYPE_PIN);
}

static void gate_on_realize(Plug *plug)
{
    Gate *obj = GATE(plug);

    /* FIXME lock sockets */
    notifier_list_add(&obj->in[0]->level_changed, &obj->in_level_changed[0]);
    notifier_list_add(&obj->in[1]->level_changed, &obj->in_level_changed[1]);

    gate_on_level_changed(obj);
}

static void gate_class_initfn(TypeClass *base_class)
{
    DeviceClass *device_class = DEVICE_CLASS(base_class);
    PlugClass *plug_class = PLUG_CLASS(base_class);

    plug_class->realize = gate_on_realize;
    device_class->visit = (DeviceVisitor *)gate_visit;
}

static const TypeInfo gate_type_info = {
    .name = TYPE_GATE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Gate),
    .instance_init = gate_initfn,
    .class_init = gate_class_initfn,
};

static void register_devices(void)
{
    type_register_static(&gate_type_info);
}

device_init(register_devices);
