#include "qemu/pci_bus.h"

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
