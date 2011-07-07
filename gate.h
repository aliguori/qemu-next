#ifndef GATE_H
#define GATE_H

#include "device.h"

typedef struct Gate
{
    Device parent;

    bool level;
} Gate;

#define TYPE_GATE "gate"
#define GATE(obj) TYPE_CHECK(Gate, obj, TYPE_GATE)

void gate_initialize(Gate *gate, const char *id);
void gate_finalize(Gate *gate);

void gate_set_level(Gate *gate, bool level);
bool gate_get_level(Gate *gate);

#endif
