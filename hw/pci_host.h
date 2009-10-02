/*
 * QEMU Common PCI Host bridge configuration data space access routines.
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* Worker routines for a PCI host controller that uses an {address,data}
   register pair to access PCI configuration space.  */

#ifndef PCI_HOST_H
#define PCI_HOST_H

#include "sysbus.h"

typedef struct {
    SysBusDevice busdev;
    uint32_t config_reg;
    PCIBus *bus;
} PCIHostState;

/* for mmio */
extern CPUWriteMemoryFunc * const pci_host_data_write_mmio[];
extern CPUReadMemoryFunc * const pci_host_data_read_mmio[];

/* for ioio */
void pci_host_data_writeb_ioport(void* opaque, uint32_t addr, uint32_t val);
void pci_host_data_writew_ioport(void* opaque, uint32_t addr, uint32_t val);
void pci_host_data_writel_ioport(void* opaque, uint32_t addr, uint32_t val);
uint32_t pci_host_data_readb_ioport(void* opaque, uint32_t addr);
uint32_t pci_host_data_readw_ioport(void* opaque, uint32_t addr);
uint32_t pci_host_data_readl_ioport(void* opaque, uint32_t addr);

typedef struct {
    PCIHostState pci;

    /* express part */
    target_phys_addr_t  base_addr;
#define PCIE_BASE_ADDR_INVALID  ((target_phys_addr_t)-1ULL)
    target_phys_addr_t  size;
    int bus_num_order;
    int mmio_index;
} PCIExpressHost;

int pcie_host_init(PCIExpressHost *e,
                   CPUReadMemoryFunc **mmcfg_read,
                   CPUWriteMemoryFunc **mmcfg_write);

void pcie_host_mmcfg_unmap(PCIExpressHost *e);
void pcie_host_mmcfg_map(PCIExpressHost *e,
                         target_phys_addr_t addr, uint32_t size);
void pcie_host_mmcfg_update(PCIExpressHost *e,
                            int enable,
                            target_phys_addr_t addr, uint32_t size);

#endif /* PCI_HOST_H */
