#ifndef PCI_DEVICE_H
#define PCI_DEVICE_H

#include "device.h"

#define PCI_CONFIG_SIZE 128

typedef struct PciDevice
{
    Device parent;

    struct PciBus *bus;

    uint8_t config[PCI_CONFIG_SIZE];
    uint8_t wmask[PCI_CONFIG_SIZE];
    uint8_t w1mask[PCI_CONFIG_SIZE];
} PciDevice;

typedef uint32_t (PciDeviceConfigRead)(PciDevice *device, uint8_t offset, int size);
typedef void (PciDeviceConfigWrite)(PciDevice *device, uint8_t offset, int size, uint32_t value);

typedef uint64_t (PciDeviceRead)(PciDevice *device, uint64_t addr, int size, bool ioio);
typedef void (PciDeviceWrite)(PciDevice *device, uint64_t addr, int size, uint64_t value, bool ioio);

typedef uint64_t (PciDeviceRegionRead)(PciDevice *device, int region, uint64_t offset, int size);
typedef void (PciDeviceRegionWrite)(PciDevice *device, int region, uint64_t offset, int size, uint64_t value);

typedef struct PciDeviceClass
{
    DeviceClass parent_class;

    PciDeviceConfigRead *config_read;
    PciDeviceConfigWrite *config_write;

    PciDeviceRead *read;
    PciDeviceWrite *write;

    PciDeviceRegionRead *region_read;
    PciDeviceRegionWrite *region_write;
} PciDeviceClass;

#define TYPE_PCI_DEVICE "pci-device"
#define PCI_DEVICE(obj) TYPE_CHECK(PciDevice, obj, TYPE_PCI_DEVICE)
#define PCI_DEVICE_CLASS(class) TYPE_CLASS_CHECK(PciDeviceClass, class, TYPE_PCI_DEVICE)
#define PCI_DEVICE_GET_CLASS(obj) TYPE_GET_CLASS(PciDeviceClass, obj, TYPE_PCI_DEVICE)

void pci_device_initialize(PciDevice *obj, const char *id);
void pci_device_finalize(PciDevice *obj);
void pci_device_visit(PciDevice *device, Visitor *v, const char *name, Error **errp);

uint32_t pci_device_config_read_std(PciDevice *device, uint8_t offset, int size);
void pci_device_config_write_std(PciDevice *device, uint8_t offset, int size, uint32_t value);

uint32_t pci_device_config_read(PciDevice *device, uint8_t offset, int size);
void pci_device_config_write(PciDevice *device, uint8_t offset, int size, uint32_t value);

uint64_t pci_device_read(PciDevice *device, uint64_t offset, int size, bool ioio);
void pci_device_write(PciDevice *device, uint64_t offset, int size, uint64_t value, bool ioio);

uint64_t pci_device_region_read(PciDevice *device, int region, uint64_t offset, int size);
void pci_device_region_write(PciDevice *device, int region, uint64_t offset, int size, uint64_t value);

bool pci_device_is_target(PciDevice *device, uint64_t addr, int size, bool ioio);

/* Config space accessors */
void pci_device_set_vendor_id(PciDevice *device, uint16_t value);
uint16_t pci_device_get_vendor_id(PciDevice *device);

void pci_device_set_device_id(PciDevice *device, uint16_t value);
uint16_t pci_device_get_device_id(PciDevice *device);

void pci_device_set_command(PciDevice *device, uint16_t value);
uint16_t pci_device_get_command(PciDevice *device);

void pci_device_set_status(PciDevice *device, uint16_t value);
uint16_t pci_device_get_status(PciDevice *device);

void pci_device_set_class_revision(PciDevice *device, uint8_t value);
uint8_t pci_device_get_class_revision(PciDevice *device);

void pci_device_set_class_prog(PciDevice *device, uint8_t value);
uint8_t pci_device_get_class_prog(PciDevice *device);

void pci_device_set_class_device(PciDevice *device, uint16_t value);
uint16_t pci_device_get_class_device(PciDevice *device);

void pci_device_set_cache_line_size(PciDevice *device, uint8_t value);
uint8_t pci_device_get_cache_line_size(PciDevice *device);

void pci_device_set_latency_timer(PciDevice *device, uint8_t value);
uint8_t pci_device_get_latency_timer(PciDevice *device);

void pci_device_set_header_type(PciDevice *device, uint8_t value);
uint8_t pci_device_get_header_type(PciDevice *device);

void pci_device_set_bist(PciDevice *device, uint8_t value);
uint8_t pci_device_get_bist(PciDevice *device);

#endif
