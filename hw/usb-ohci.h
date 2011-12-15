#ifndef QEMU_USB_OHCI_H
#define QEMU_USB_OHCI_H

#include "qemu-common.h"
#include "usb.h"

USBBus *usb_ohci_init_pci(struct PCIBus *bus, int devfn);

#endif

