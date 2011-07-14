#ifndef PCI_DEVICE_H
#define PCI_DEVICE_H

#include "device.h"

typedef struct PCIDevice
{
    Device parent;

    struct PCIBus *bus;

    uint8_t config[256];
    uint8_t wmask[256];
    uint8_t w1mask[256];
} PCIDevice;

typedef uint64_t (PCIDeviceRead)(PCIDevice *device, int region, uint64_t offset, int size);
typedef void (PCIDeviceWrite)(PCIDevice *device, int region, uint64_t offset, int size, uint64_t value);

typedef struct PCIDeviceClass
{
    DeviceClass parent_class;

    PCIDeviceRead *read;
    PCIDeviceWrite *write;
} PCIDeviceClass;

void pci_device_initialize(PCIDevice *obj, const char *id);
void pci_device_finalize(PCIDevice *obj);
void pci_device_visit(PCIDevice *device, Visitor *v, const char *name, Error **errp);

uint32_t pci_device_config_read(PCIDevice *device, uint8_t offset, int size);
void pci_device_config_write(PCIDevice *device, uint8_t offset, int size, uint8_t value);

uint64_t pci_device_region_read(PCIDevice *device, int region, uint64_t offset, int size);
void pci_device_region_write(PCIDevice *device, int region, uint64_t offset, int size, uint64_t value);

/* Config space accessors */
void pci_device_set_vendor_id(PCIDevice *device, uint16_t value);
uint16_t pci_device_get_vendor_id(PCIDevice *device);

void pci_device_set_device_id(PCIDevice *device, uint16_t value);
uint16_t pci_device_get_device_id(PCIDevice *device);

void pci_device_set_command(PCIDevice *device, uint16_t value);
uint16_t pci_device_get_command(PCIDevice *device);

void pci_device_set_status(PCIDevice *device, uint16_t value);
uint16_t pci_device_get_status(PCIDevice *device);

void pci_device_set_class_revision(PCIDevice *device, uint8_t value);
uint8_t pci_device_get_class_revision(PCIDevice *device);

void pci_device_set_class_prog(PCIDevice *device, uint8_t value);
uint8_t pci_device_get_class_prog(PCIDevice *device);

void pci_device_set_class_device(PCIDevice *device, uint16_t value);
uint16_t pci_device_get_class_device(PCIDevice *device);

void pci_device_set_cache_line_size(PCIDevice *device, uint8_t value);
uint8_t pci_device_get_cache_line_size(PCIDevice *device);

void pci_device_set_latency_timer(PCIDevice *device, uint8_t value);
uint8_t pci_device_get_latency_timer(PCIDevice *device);

void pci_device_set_header_type(PCIDevice *device, uint8_t value);
uint8_t pci_device_get_header_type(PCIDevice *device);

void pci_device_set_bist(PCIDevice *device, uint8_t value);
uint8_t pci_device_get_bist(PCIDevice *device);

#endif
