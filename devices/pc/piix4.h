#ifndef PIIX4_H
#define PIIX4_H

#include "qemu/device.h"
#include "qemu/pci.h"

/*
 * Along side the I440FX, there is also an PCI-ISA Bridge and Super I/O
 * chip known as the PIIX.  The PIIX also includes an IDE host controller and
 * potentially a UHCI controller.
 *
 * All interaction with the I440FX is through PCI.  The PMC and host bridge is
 * accessible as device 0, fn 0.  The PIIX PCI-ISA bridge is accessible via
 * device 1, fn 0.  fn 1 contains the IDE controller, function 2 contains
 * the UHCI controller, and function 3 contains the power management device.
 */

typedef struct PIIX4
{
    Device parent;

    PCIDevice *slot[32];
} PIIX4;

#define TYPE_PIIX4 "piix4"
#define PIIX4(obj) TYPE_CHECK(PIIX4, obj, TYPE_PIIX4)

void piix4_initialize(PIIX4 *obj, const char *id);
void piix4_finalize(PIIX4 *obj);

#endif
