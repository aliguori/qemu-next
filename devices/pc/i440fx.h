#ifndef I440FX_H
#define I440FX_H

#include "device.h"

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

    PCIDevice *slots[32];
} I440FX;

#define TYPE_I440FX "i440fx"
#define I440FX(obj) TYPE_CHECK(I440FX, obj, TYPE_I440FX)

void i440fx_initialize(I440FX *obj, const char *id);
void i440fx_finalize(I440FX *obj);

/* A word on interfaces.
 *
 * Interfaces are normal types.  They are dynamically allocated during a
 * instance's initialization and then associated with that interface.  Many
 * interface objects may be associaed with a single instance.
 *
 * The actual class of an interface object is a subclass of the interface.  This
 * class is not normally visible to the user though as only the super class is
 * returned.
 *
 * Interfaces can be derived from other interfaces but the type system enforces
 * that no data can be associated with an interface.
 */

static void i440fx_pci_bus_initfn(TypeClass *class)
{
    PCIBusClass *bus_class = PCI_BUS_CLASS(class);

    bus_class->read = i440fx_pci_read;
    bus_class->read_dma = i440fx_pci_read_dma;
    bus_class->write = i440fx_pci_write;
    bus_class->write_dma = i440fx_pci_write_dma;
}

static const TypeInfo i440fx_type_info = {
    .name = TYPE_I440FX,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(I440FX),
    .class_size = sizeof(I440FXClass),
    .class_init = i440fx_class_initfn,
    .instance_init = i440fx_initfn,
    .interfaces = (InterfaceInfo[]){
        {
            .type = TYPE_PCI_BUS,
            .interface_init = i440fx_pci_bus_initfn,
        },
        { }
    },
};

#endif
