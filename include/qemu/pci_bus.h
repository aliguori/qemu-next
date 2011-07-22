#ifndef PCI_BUS_H
#define PCI_BUS_H

#include "qemu/type.h"
#include "qemu/pci_device.h"

typedef struct PciBus
{
    Interface parent;
} PciBus;

typedef struct PciBusClass
{
    InterfaceClass parent_interface;

    uint64_t (*read)(PciBus *bus, PciDevice *dev, uint64_t addr, int size);
    void (*write)(PciBus *bus, PciDevice *dev, uint64_t addr, int size, uint64_t value);

    void (*update_irq)(PciBus *bus, PciDevice *dev);
} PciBusClass;

#define TYPE_PCI_BUS "pci-bus"
#define PCI_BUS(obj) TYPE_CHECK(PciBus, obj, TYPE_PCI_BUS)
#define PCI_BUS_CLASS(class) TYPE_CLASS_CHECK(PciBusClass, class, TYPE_PCI_BUS)
#define PCI_BUS_GET_CLASS(obj) TYPE_GET_CLASS(PciBusClass, obj, TYPE_PCI_BUS)

uint64_t pci_bus_read(PciBus *bus, PciDevice *dev, uint64_t addr, int size);
void pci_bus_write(PciBus *bus, PciDevice *dev, uint64_t addr, int size, uint64_t value);
void pci_bus_update_irq(PciBus *bus, PciDevice *dev);

#endif
