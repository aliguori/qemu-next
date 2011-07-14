#include "qemu/i440fx.h"

static uint64_t i440fx_pci_bus_read(PciBus *bus, PciDevice *dev, uint64_t addr, int size)
{
    return ~(uint64_t)0;
}

static void i440fx_pci_bus_write(PciBus *bus, PciDevice *dev, uint64_t addr, int size, uint64_t value)
{
}

static void i440fx_pci_bus_update_irq(PciBus *bus, PciDevice *dev)
{
}

static void i440fx_pci_bus_initfn(TypeClass *class)
{
    PciBusClass *bus_class = PCI_BUS_CLASS(class);

    bus_class->read = i440fx_pci_bus_read;
    bus_class->write = i440fx_pci_bus_write;
    bus_class->update_irq = i440fx_pci_bus_update_irq;
}

void i440fx_initialize(I440FX *obj, const char *id)
{
    type_initialize(obj, TYPE_I440FX, id);
}

void i440fx_finalize(I440FX *obj)
{
    type_finalize(obj);
}

static int64_t i440fx_get_test(Plug *plug)
{
    I440FX *obj = I440FX(plug);

    printf("%p\n", I440FX(plug));
    printf("%p\n", PCI_BUS(obj));
    printf("%p\n", I440FX(PCI_BUS(obj)));

    return 42;
}

static void i440fx_initfn(TypeInstance *inst)
{
    I440FX *obj = I440FX(inst);

    plug_add_property_int(PLUG(obj), "test", i440fx_get_test, NULL, PROP_F_READ);
}

static const TypeInfo i440fx_type_info = {
    .name = TYPE_I440FX,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(I440FX),
    .instance_init = i440fx_initfn,
    .interfaces = (InterfaceInfo[]){
        {
            .type = TYPE_PCI_BUS,
            .interface_initfn = i440fx_pci_bus_initfn,
        },
        { }
    },
};

static void register_devices(void)
{
    type_register_static(&i440fx_type_info);
}

device_init(register_devices);
