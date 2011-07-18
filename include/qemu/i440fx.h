#ifndef I440FX_H
#define I440FX_H

#include "qemu/device.h"
#include "qemu/pci_bus.h"
#include "qemu/pci_device.h"
#include "qemu/rom.h"

/**
 * The I440FX is the main chipset in a classic Pentium-era PC.
 *
 * The I440FX consists of two parts, the Memory Controller and the PCI Host
 * Bridge.  This is collectively referred to as the PMC.
 *
 */

typedef struct I440FX
{
    PciDevice parent;

    int config_index;
    uint8_t config[256];

    RomDevice bios;

    uint64_t max_ram_offset;

    PciDevice *slots[32];
} I440FX;

#define TYPE_I440FX "i440fx"
#define I440FX(obj) TYPE_CHECK(I440FX, obj, TYPE_I440FX)

void i440fx_initialize(I440FX *obj, const char *id);
void i440fx_finalize(I440FX *obj);

void i440fx_mm_write(I440FX *obj, uint64_t addr, int size, uint64_t value);
uint64_t i440fx_mm_read(I440FX *obj, uint64_t addr, int size);

void i440fx_pio_write(I440FX *obj, uint16_t addr, int size, uint32_t value);
uint32_t i440fx_pio_read(I440FX *obj, uint16_t addr, int size);

#endif
