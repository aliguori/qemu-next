#ifndef LOGIC_GATES_H
#define LOGIC_GATES_H

#include "qemu/gate.h"

typedef struct AndGate
{
    Gate parent;
} AndGate;

#define TYPE_AND_GATE "and-gate"
#define AND_GATE(obj) TYPE_CHECK(AndGate, obj, TYPE_AND_GATE)

void and_gate_initialize(AndGate *gate, const char *id);
void and_gate_finalize(AndGate *gate);

typedef struct OrGate
{
    Gate parent;
} OrGate;

#define TYPE_OR_GATE "or-gate"
#define OR_GATE(obj) TYPE_CHECK(OrGate, obj, TYPE_OR_GATE)

void or_gate_initialize(OrGate *gate, const char *id);
void or_gate_finalize(OrGate *gate);

typedef struct XorGate
{
    Gate parent;
} XorGate;

#define TYPE_XOR_GATE "xor-gate"
#define XOR_GATE(obj) TYPE_CHECK(XorGate, obj, TYPE_XOR_GATE)

void xor_gate_initialize(XorGate *gate, const char *id);
void xor_gate_finalize(XorGate *gate);

#endif
