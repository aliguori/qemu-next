#include "qemu/logic_gates.h"

void and_gate_initialize(AndGate *gate, const char *id)
{
    type_initialize(gate, TYPE_AND_GATE, id);
}

void and_gate_finalize(AndGate *gate)
{
    type_finalize(gate);
}

static bool and_gate_compute(Gate *gate, bool in0, bool in1)
{
    return in0 & in1;
}

static void and_gate_class_initfn(TypeClass *base_class)
{
    GateClass *gate_class = GATE_CLASS(base_class);

    gate_class->compute = and_gate_compute;
}

static const TypeInfo and_gate_type_info = {
    .name = TYPE_AND_GATE,
    .parent = TYPE_GATE,
    .instance_size = sizeof(AndGate),
    .class_init = and_gate_class_initfn,
};

void or_gate_initialize(OrGate *gate, const char *id)
{
    type_initialize(gate, TYPE_OR_GATE, id);
}

void or_gate_finalize(OrGate *gate)
{
    type_finalize(gate);
}

static bool or_gate_compute(Gate *gate, bool in0, bool in1)
{
    return in0 | in1;
}

static void or_gate_class_initfn(TypeClass *base_class)
{
    GateClass *gate_class = GATE_CLASS(base_class);

    gate_class->compute = or_gate_compute;
}

static const TypeInfo or_gate_type_info = {
    .name = TYPE_OR_GATE,
    .parent = TYPE_GATE,
    .instance_size = sizeof(OrGate),
    .class_init = or_gate_class_initfn,
};

void xor_gate_initialize(XorGate *gate, const char *id)
{
    type_initialize(gate, TYPE_XOR_GATE, id);
}

void xor_gate_finalize(XorGate *gate)
{
    type_finalize(gate);
}

static bool xor_gate_compute(Gate *gate, bool in0, bool in1)
{
    return in0 ^ in1;
}

static void xor_gate_class_initfn(TypeClass *base_class)
{
    GateClass *gate_class = GATE_CLASS(base_class);

    gate_class->compute = xor_gate_compute;
}

static const TypeInfo xor_gate_type_info = {
    .name = TYPE_XOR_GATE,
    .parent = TYPE_GATE,
    .instance_size = sizeof(XorGate),
    .class_init = xor_gate_class_initfn,
};

static void register_devices(void)
{
    type_register_static(&and_gate_type_info);
    type_register_static(&or_gate_type_info);
    type_register_static(&xor_gate_type_info);
}

device_init(register_devices);
