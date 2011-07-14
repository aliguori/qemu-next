#ifndef I440FX_H
#define I440FX_H

#include "device.h"
#include "pci_bus.h"

/**
 * The I440FX is the main chipset in a classic Pentium-era PC.
 *
 * The I440FX consists of two parts, the Memory Controller and the PCI Host
 * Bridge.  This is collectively referred to as the PMC.
 *
 */

typedef struct I440FX
{
    Device parent;
} I440FX;

#define TYPE_I440FX "i440fx"
#define I440FX(obj) TYPE_CHECK(I440FX, obj, TYPE_I440FX)

void i440fx_initialize(I440FX *obj, const char *id);
void i440fx_finalize(I440FX *obj);

#endif
