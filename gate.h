#ifndef GATE_H
#define GATE_H

#include "device.h"
#include "pin.h"

typedef struct Gate
{
    Device parent;

    Pin *in[2];
    Pin out;

    /* private */
    Notifier in_level_changed[2];
} Gate;

typedef struct GateClass
{
    DeviceClass parent_class;

    /* protected */
    bool (*compute)(Gate *gate, bool in0, bool in1);
} GateClass;

#define TYPE_GATE "gate"
#define GATE(obj) TYPE_CHECK(Gate, obj, TYPE_GATE)
#define GATE_CLASS(class) TYPE_CLASS_CHECK(GateClass, class, TYPE_GATE)

void gate_initialize(Gate *gate, const char *id);
void gate_finalize(Gate *gate);

#endif
