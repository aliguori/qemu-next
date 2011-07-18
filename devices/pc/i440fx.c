#include "qemu/i440fx.h"
#include "hw/pci_ids.h"

#define I440FX_SMRAM (0x72)

#define I440FX_PAM0 0x59
#define I440FX_PAM1 0x5A
#define I440FX_PAM2 0x5B
#define I440FX_PAM3 0x5C
#define I440FX_PAM4 0x5D
#define I440FX_PAM5 0x5E
#define I440FX_PAM6 0x5F

#define PAM_RE_HIGH 0x10
#define PAM_RE_LOW  0x01

#define PAM_WE_HIGH 0x20
#define PAM_WE_LOW  0x02

void i440fx_initialize(I440FX *obj, const char *id)
{
    type_initialize(obj, TYPE_I440FX, id);
}

void i440fx_finalize(I440FX *obj)
{
    type_finalize(obj);
}

static bool addr_in_range(uint64_t addr, int size, uint64_t start, uint64_t end)
{
    return (addr >= start) && ((addr + size) < end);
}

static PciDevice *i440fx_pci_devsel(I440FX *obj, uint64_t addr, int size, bool ioio)
{
    PciDevice *device = NULL;
    int i;

    /* We walk through each slot and determine if this device, or any of it's functions
     * are responsible for this I/O operation.
     *
     * There are two possible ways that we'll identify that a device owns an I/O operation.
     *
     * 1) If a device as a BAR that maps the address, we'll identify the device and stop
     *    scanning the bus. This is positive decoding.
     *
     * 2) If no devices are positively identified through (1), then we will use the first
     *    device that advertises itself as an ISA bridge.
     *
     * 3) As a very special case, the VGA range is always forwarded to the first VGA device
     *    on the bus.
     */
    for (i = 0; i < 32; i++) {
        if (obj->slots[i] == NULL) {
            continue;
        }

        /* This is a little different than what hardware does.  Hardware always presents
         * this accesses to the PCI bus and the VGA device will assert #DEVSEL.  But since
         * we're keeping pci_device_is_target() simple, we implement the logic here.
         */
        if (addr_in_range(addr, size, 0xA0000, 0xC0000) &&
            pci_device_get_class_device(obj->slots[i]) == PCI_CLASS_DISPLAY_VGA) {
            device = obj->slots[i];
            break;
        }
            

        if (!device && pci_device_get_class_device(obj->slots[i]) == PCI_CLASS_BRIDGE_ISA) {
            device = obj->slots[i];
        }

        if (pci_device_is_target(obj->slots[i], addr, size, true)) {
            device = obj->slots[i];
            break;
        }
    }

    return device;
}

static void i440fx_ram_write(I440FX *obj, uint64_t addr, int size, uint64_t value)
{
}

static uint64_t i440fx_ram_read(I440FX *obj, uint64_t addr, int size)
{
    return 0;
}

static void i440fx_rom_write(I440FX *obj, uint64_t addr, int size, uint64_t value)
{
}

static uint64_t i440fx_rom_read(I440FX *obj, uint64_t addr, int size)
{
    uint64_t value;
    uint32_t bios_offset;

    bios_offset = (uint32_t)-rom_device_get_size(&obj->bios);

    if (addr_in_range(addr, size, bios_offset, 1ULL << 32)) {
        value = rom_device_read(&obj->bios, addr - bios_offset, size);
    } else if (addr_in_range(addr, size, 0xF0000, 0x100000)) {
        value = rom_device_read(&obj->bios, addr - 0xF0000, size);
    } else {
        value = 0;
    }

    return value;
}

static void i440fx_pci_host_write(I440FX *obj, uint8_t address, int size, uint32_t value)
{
    uint8_t bus = (address >> 16) & 0xFF;
    uint8_t devfn = (address >> 8) & 0xFF;
    uint8_t offset = (address >> 0) & 0xFF;
    uint8_t slot = (devfn >> 3) & 0x1F;

    /* FIXME fn's other than 0 */
    if (bus == 0 && obj->slots[slot] && devfn == (slot << 3)) {
        pci_device_config_write(obj->slots[slot], offset, size, value);
    }
}

static uint32_t i440fx_pci_host_read(I440FX *obj, uint8_t address, int size)
{
    uint8_t bus = (address >> 16) & 0xFF;
    uint8_t devfn = (address >> 8) & 0xFF;
    uint8_t offset = (address >> 0) & 0xFF;
    uint8_t slot = (devfn >> 3) & 0x1F;
    uint32_t value = ~0;

    /* FIXME fn's other than 0 */
    if (bus == 0 && obj->slots[slot] && devfn == (slot << 3)) {
        value = pci_device_config_read(obj->slots[slot], offset, size);
    }

    return value;
}

void i440fx_mm_write(I440FX *obj, uint64_t addr, int size, uint64_t value)
{
    uint32_t bios_offset;

    bios_offset = (uint32_t)-rom_device_get_size(&obj->bios);

    if (addr_in_range(addr, size, 0xC0000, 0xF0000)) {
        /* legacy ROM region */
        int segment = (addr - 0xC0000) / 0x4000 + 2;
        uint8_t enable_mask = (segment & 0x01) ? PAM_WE_HIGH : PAM_WE_LOW;

        if ((obj->config[I440FX_PAM0 + (segment / 2) + 1] & enable_mask)) {
            i440fx_ram_write(obj, addr, size, value);
        }
    } else if (addr_in_range(addr, size, 0xF0000, 0x100000)) {
        /* legacy BIOS ROM region */
        if ((obj->config[I440FX_PAM0] & PAM_WE_HIGH)) {
            i440fx_ram_write(obj, addr, size, value);
        } else {
            i440fx_rom_write(obj, addr, size, value);
        }
    } else if (addr >= obj->max_ram_offset ||
               addr_in_range(addr, size, 0xE00000000, bios_offset)) {
        /* PCI holes */
        PciDevice *device = i440fx_pci_devsel(obj, addr, size, false);

        if (device) {
            pci_device_write(device, addr, size, value, false);
        }
    } else if (addr_in_range(addr, size, bios_offset, 1ULL << 32)) {
        /* extended BIOS ROM region */
        i440fx_rom_write(obj, addr, size, value);
    } else {
        /* RAM */
        i440fx_ram_write(obj, addr, size, value);
    }
}

uint64_t i440fx_mm_read(I440FX *obj, uint64_t addr, int size)
{
    uint64_t value = 0;
    uint32_t bios_offset;

    bios_offset = (uint32_t)-rom_device_get_size(&obj->bios);

    if (addr_in_range(addr, size, 0xC0000, 0xF0000)) {
        /* legacy ROM region */
        int segment = (addr - 0xC0000) / 0x4000 + 2;
        uint8_t enable_mask = (segment & 0x01) ? PAM_RE_HIGH : PAM_RE_LOW;

        if ((obj->config[I440FX_PAM0 + (segment / 2) + 1] & enable_mask)) {
            value = i440fx_ram_read(obj, addr, size);
        } else {
            value = i440fx_rom_read(obj, addr, size);
        }
    } else if (addr_in_range(addr, size, 0xF0000, 0x100000)) {
        /* legacy BIOS ROM region */
        if ((obj->config[I440FX_PAM0] & PAM_RE_HIGH)) {
            value = i440fx_ram_read(obj, addr, size);
        } else {
            value = i440fx_rom_read(obj, addr, size);
        }
    } else if (addr >= obj->max_ram_offset ||
               addr_in_range(addr, size, 0xE00000000, bios_offset)) {
        /* PCI holes */
        PciDevice *device = i440fx_pci_devsel(obj, addr, size, false);

        if (device) {
            value = pci_device_read(device, addr, size, false);
        }
    } else if (addr_in_range(addr, size, bios_offset, 1ULL << 32)) {
        /* extended BIOS ROM region */
        value = i440fx_rom_read(obj, addr, size);
    } else {
        /* RAM */
        value = i440fx_ram_read(obj, addr, size);
    }

    return value;
}

void i440fx_pio_write(I440FX *obj, uint16_t addr, int size, uint32_t value)
{
    if (addr_in_range(addr, size, 0x0CF8, 0x0CFC)) {
        /* PCI config latch register */
        obj->config_index = value;
    } else if (addr >= 0x0CFC && addr < 0xD00) {
        /* PCI config data register */
        if ((obj->config_index & (1 << 31))) {
            i440fx_pci_host_write(obj, obj->config_index | (addr & 3), size, value);
        }
    } else {
        /* Pass onto bus */
        PciDevice *device = i440fx_pci_devsel(obj, addr, size, true);

        if (device) {
            pci_device_write(device, addr, size, value, true);
        }
    }
}

uint32_t i440fx_pio_read(I440FX *obj, uint16_t addr, int size)
{
    uint32_t value = (uint32_t)-1;

    if (addr_in_range(addr, size, 0x0CF8, 0x0CFC)) {
        /* PCI config latch register */
        value = obj->config_index;
    } else if (addr_in_range(addr, size, 0x0CFC, 0x0D00)) {
        /* PCI config data register */
        if ((obj->config_index & (1 << 31))) {
            value = i440fx_pci_host_read(obj, obj->config_index | (addr & 3), size);
        }
    } else {
        /* Pass onto bus */
        PciDevice *device = i440fx_pci_devsel(obj, addr, size, true);

        if (device) {
            value = pci_device_read(device, addr, size, true);
        }
    }

    return value;
}

static uint64_t i440fx_pci_bus_read(PciBus *bus, PciDevice *dev, uint64_t addr, int size)
{
    return i440fx_mm_read(I440FX(bus), addr, size);
}

static void i440fx_pci_bus_write(PciBus *bus, PciDevice *dev, uint64_t addr, int size, uint64_t value)
{
    i440fx_mm_write(I440FX(bus), addr, size, value);
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

static void i440fx_initfn(TypeInstance *inst)
{
    I440FX *obj = I440FX(inst);
    PciDevice *pci_dev = PCI_DEVICE(obj);
    char buffer[128];
    int i;

    snprintf(buffer, sizeof(buffer), "%s::bios", type_get_id(inst));
    rom_device_initialize(&obj->bios, buffer);

    /* slot[0] is reserved for the host controller */
    for (i = 1; i < 32; i++) {
        char buffer[32];

        snprintf(buffer, sizeof(buffer), "slot[%d]", i);
        plug_add_property_socket(PLUG(obj), buffer, (Plug **)&obj->slots[i], TYPE_PCI_DEVICE);
    }

    plug_add_property_plug(PLUG(obj), "bios", PLUG(&obj->bios), TYPE_ROM_DEVICE);

    obj->slots[0] = pci_dev;

    pci_device_set_vendor_id(pci_dev, PCI_VENDOR_ID_INTEL);
    pci_device_set_device_id(pci_dev, PCI_DEVICE_ID_INTEL_82441);
    pci_device_set_class_revision(pci_dev, 0x02);
    pci_device_set_class_device(pci_dev, PCI_CLASS_BRIDGE_HOST);
}

static const TypeInfo i440fx_type_info = {
    .name = TYPE_I440FX,
    .parent = TYPE_PCI_DEVICE,
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
