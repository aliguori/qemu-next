#include "qemu/pci_device.h"
#include "qemu/pci_bus.h"
#include "hw/pci_regs.h"

void pci_device_initialize(PciDevice *obj, const char *id)
{
    type_initialize(obj, TYPE_PCI_DEVICE, id);
}

void pci_device_finalize(PciDevice *obj)
{
    type_finalize(obj);
}

void pci_device_visit(PciDevice *device, Visitor *v, const char *name, Error **errp)
{
    device_visit(DEVICE(device), v, name, errp);
}

static void pci_device_initfn(TypeInstance *inst)
{
    PciDevice *obj = PCI_DEVICE(inst);

    plug_add_property_socket(PLUG(obj), "bus", (Plug **)&obj->bus, TYPE_PCI_BUS);
}

uint32_t pci_device_config_read_std(PciDevice *device, uint8_t offset, int size)
{
    if (size == 1) {
        return device->config[offset];
    } else if (size == 2) {
        return (device->config[offset] << 0) | (device->config[offset + 1] << 8);
    } else if (size == 4) {
        return (device->config[offset] << 0) | (device->config[offset + 1] << 8) |
            (device->config[offset + 2] << 16) | (device->config[offset + 3] << 24);
    } else {
        return ~0;
    }
}

void pci_device_config_write_std(PciDevice *device, uint8_t offset, int size, uint32_t value)
{
    if (size == 1) {
        device->config[offset] = value & 0xFF;
    } else if (size == 2) {
        device->config[offset] = value & 0xFF;
        device->config[offset + 1] = (value >> 8) & 0xFF;
    } else if (size == 4) {
        device->config[offset] = value & 0xFF;
        device->config[offset + 1] = (value >> 8) & 0xFF;
        device->config[offset + 2] = (value >> 16) & 0xFF;
        device->config[offset + 3] = (value >> 24) & 0xFF;
    }
}

static void pci_device_class_initfn(TypeClass *class)
{
    PciDeviceClass *pci_class = PCI_DEVICE_CLASS(class);

    pci_class->config_read = pci_device_config_read_std;
    pci_class->config_write = pci_device_config_write_std;
}

uint32_t pci_device_config_read(PciDevice *device, uint8_t offset, int size)
{
    return PCI_DEVICE_GET_CLASS(device)->config_read(device, offset, size);
}

void pci_device_config_write(PciDevice *device, uint8_t offset, int size, uint32_t value)
{
    PCI_DEVICE_GET_CLASS(device)->config_write(device, offset, size, value);
}

uint64_t pci_device_read(PciDevice *device, uint64_t offset, int size)
{
    return PCI_DEVICE_GET_CLASS(device)->read(device, offset, size);
}

void pci_device_write(PciDevice *device, uint64_t offset, int size, uint64_t value)
{
    PCI_DEVICE_GET_CLASS(device)->write(device, offset, size, value);
}

uint64_t pci_device_region_read(PciDevice *device, int region, uint64_t offset, int size)
{
    return PCI_DEVICE_GET_CLASS(device)->region_read(device, region, offset, size);
}

void pci_device_region_write(PciDevice *device, int region, uint64_t offset, int size, uint64_t value)
{
    PCI_DEVICE_GET_CLASS(device)->region_write(device, region, offset, size, value);
}

void pci_device_set_vendor_id(PciDevice *device, uint16_t value)
{
    pci_device_config_write(device, PCI_VENDOR_ID, 2, value);
}

uint16_t pci_device_get_vendor_id(PciDevice *device)
{
    return pci_device_config_read(device, PCI_VENDOR_ID, 2);
}

void pci_device_set_device_id(PciDevice *device, uint16_t value)
{
    pci_device_config_write(device, PCI_DEVICE_ID, 2, value);
}

uint16_t pci_device_get_device_id(PciDevice *device)
{
    return pci_device_config_read(device, PCI_DEVICE_ID, 2);
}

void pci_device_set_command(PciDevice *device, uint16_t value)
{
    pci_device_config_write(device, PCI_COMMAND, 2, value);
}

uint16_t pci_device_get_command(PciDevice *device)
{
    return pci_device_config_read(device, PCI_COMMAND, 2);
}

void pci_device_set_status(PciDevice *device, uint16_t value)
{
    pci_device_config_write(device, PCI_STATUS, 2, value);
}

uint16_t pci_device_get_status(PciDevice *device)
{
    return pci_device_config_read(device, PCI_STATUS, 2);
}

void pci_device_set_class_revision(PciDevice *device, uint8_t value)
{
    pci_device_config_write(device, PCI_CLASS_REVISION, 1, value);
}

uint8_t pci_device_get_class_revision(PciDevice *device)
{
    return pci_device_config_read(device, PCI_CLASS_REVISION, 1);
}

void pci_device_set_class_prog(PciDevice *device, uint8_t value)
{
    pci_device_config_write(device, PCI_CLASS_PROG, 1, value);
}

uint8_t pci_device_get_class_prog(PciDevice *device)
{
    return pci_device_config_read(device, PCI_CLASS_PROG, 1);
}

void pci_device_set_class_device(PciDevice *device, uint16_t value)
{
    pci_device_config_write(device, PCI_CLASS_DEVICE, 2, value);
}

uint16_t pci_device_get_class_device(PciDevice *device)
{
    return pci_device_config_read(device, PCI_CLASS_DEVICE, 2);
}

void pci_device_set_cache_line_size(PciDevice *device, uint8_t value)
{
    pci_device_config_write(device, PCI_CACHE_LINE_SIZE, 1, value);
}

uint8_t pci_device_get_cache_line_size(PciDevice *device)
{
    return pci_device_config_read(device, PCI_CACHE_LINE_SIZE, 1);
}

void pci_device_set_latency_timer(PciDevice *device, uint8_t value)
{
    pci_device_config_write(device, PCI_LATENCY_TIMER, 1, value);
}

uint8_t pci_device_get_latency_timer(PciDevice *device)
{
    return pci_device_config_read(device, PCI_LATENCY_TIMER, 1);
}

void pci_device_set_header_type(PciDevice *device, uint8_t value)
{
    pci_device_config_write(device, PCI_HEADER_TYPE, 1, value);
}

uint8_t pci_device_get_header_type(PciDevice *device)
{
    return pci_device_config_read(device, PCI_HEADER_TYPE, 1);
}

void pci_device_set_bist(PciDevice *device, uint8_t value)
{
    pci_device_config_write(device, PCI_BIST, 1, value);
}

uint8_t pci_device_get_bist(PciDevice *device)
{
    return pci_device_config_read(device, PCI_BIST, 1);
}

static TypeInfo pci_device_info = {
    .name = TYPE_PCI_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(PciDevice),
    .class_size = sizeof(PciDeviceClass),
    .instance_init = pci_device_initfn,
    .class_init = pci_device_class_initfn,
};

static void register_devices(void)
{
    type_register_static(&pci_device_info);
}

device_init(register_devices);
