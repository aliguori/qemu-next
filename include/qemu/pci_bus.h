#ifndef PCI_BUS_H
#define PCI_BUS_H

#include "type.h"

typedef struct PciDevice PciDevice;

typedef struct PciBus
{
    Interface parent;
} PciBus;

typedef struct PciBusClass
{
    TypeInterface parent_interface;

    uint64_t (*read)(PciBus *bus, PciDevice *dev, uint64_t addr, int size);
    void (*write)(PciBus *bus, PciDevice *dev, uint64_t addr, int size, uint64_t value);

    void (*update_irq)(PciBus *bus, PciDevice *dev);
} PciBusClass;

#define TYPE_PCI_BUS "pci-bus"
#define PCI_BUS(obj) TYPE_CHECK(PciBus, obj, TYPE_PCI_BUS)
#define PCI_BUS_CLASS(obj) TYPE_CLASS_CHECK(PciBusClass, (obj), TYPE_PCI_BUS)

#endif
