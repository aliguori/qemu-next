#include "qemu/pci_bus.h"

uint64_t pci_bus_read(PciBus *bus, PciDevice *dev, uint64_t addr, int size)
{
    return PCI_BUS_GET_CLASS(bus)->read(bus, dev, addr, size);
}

void pci_bus_write(PciBus *bus, PciDevice *dev, uint64_t addr, int size, uint64_t value)
{
    PCI_BUS_GET_CLASS(bus)->write(bus, dev, addr, size, value);
}

void pci_bus_update_irq(PciBus *bus, PciDevice *dev)
{
    PCI_BUS_GET_CLASS(bus)->update_irq(bus, dev);
}

static TypeInfo pci_bus_info = {
    .name = TYPE_PCI_BUS,
    .parent = TYPE_INTERFACE,
    .instance_size = sizeof(PciBus),
    .class_size = sizeof(PciBusClass),
};

static void register_devices(void)
{
    type_register_static(&pci_bus_info);
}

device_init(register_devices);
