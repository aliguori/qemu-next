#ifndef PCI_H
#define PCI_H

typedef struct PCIBus PCIBus;
typedef struct PCIBusClass PCIBusClass;

struct PCIBusClass
{
    TypeClass parent_class;

    uint64_t (*read)(PCIBus *bus, PCIDevice *dev, uint64_t addr, int size);
    void (*read_dma)(PCIBus *bus, PCIDevice *dev, uint64_t addr, int size, void *data);

    void (*write)(PCIBus *bus, PCIDevice *dev, uint64_t addr, int size, uint64_t value);
    void (*write_dma)(PCIBus *bus, PCIDevice *dev, uint64_t addr, int size, const void *data);
};

struct PCIBus
{
    TypeInstance parent;
};

typedef struct PCIDevice
{
    Device parent;

    PCIBus *bus;

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

#endif
