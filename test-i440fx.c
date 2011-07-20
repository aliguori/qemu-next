#include "qemu-common.h"
#include "test-type-stub.h"
#include "qemu/i440fx.h"
#include "hw/pci_ids.h"
#include "hw/pci_regs.h"

#include <glib.h>

static uint16_t pci_config_readw(I440FX *bus, uint32_t devfn, uint8_t offset)
{
    i440fx_pio_write(bus, 0xcf8, 1, (devfn << 8) | offset | (1 << 31));
    return i440fx_pio_read(bus, 0xcfc, 2);
}

#define g_assert_cmpw_config(obj, devfn, id, op, value)  \
    g_assert_cmpint(pci_config_readw((obj), (devfn), (id)), op, (value))

int main(int argc, char **argv)
{
    I440FX chipset;

    test_type_stub_init();

    i440fx_initialize(&chipset, "chipset");

    g_assert_cmpw_config(&chipset, 0, PCI_VENDOR_ID, ==, PCI_VENDOR_ID_INTEL);
    g_assert_cmpw_config(&chipset, 0, PCI_DEVICE_ID, ==, PCI_DEVICE_ID_INTEL_82441);

    i440fx_finalize(&chipset);

    return 0;
}
