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

#ifndef PCI_HOST_C

/* debug PCI */
//#define DEBUG_PCI

#include "pci.h"
#include "pci_host.h"

#ifdef DEBUG_PCI
#define PCI_DPRINTF(fmt, ...) \
do { printf("pci_host_data: " fmt , ## __VA_ARGS__); } while (0)
#else
#define PCI_DPRINTF(fmt, ...)
#endif

#define PCI_HOST_C

#define PCI_ADDR_T      target_phys_addr_t
#define PCI_HOST_SUFFIX _mmio
#define PCI_HOST_STATIC static

#include "pci_host.c"

CPUWriteMemoryFunc * const pci_host_data_write_mmio[] = {
    &glue(pci_host_data_writeb, PCI_HOST_SUFFIX),
    &glue(pci_host_data_writew, PCI_HOST_SUFFIX),
    &glue(pci_host_data_writel, PCI_HOST_SUFFIX),
};

CPUReadMemoryFunc * const pci_host_data_read_mmio[] = {
    &glue(pci_host_data_readb, PCI_HOST_SUFFIX),
    &glue(pci_host_data_readw, PCI_HOST_SUFFIX),
    &glue(pci_host_data_readl, PCI_HOST_SUFFIX),
};

#undef PCI_ADDR_T
#undef PCI_HOST_SUFFIX
#undef PCI_HOST_STATIC

#define PCI_ADDR_T      uint32_t
#define PCI_HOST_SUFFIX _ioport
#define PCI_HOST_STATIC /* nothing. export this function */

#include "pci_host.c"

#else

PCI_HOST_STATIC void glue(pci_host_data_writeb, PCI_HOST_SUFFIX)(
    void* opaque, PCI_ADDR_T addr, uint32_t val)
{
    PCIHostState *s = opaque;

    PCI_DPRINTF("writeb addr " TARGET_FMT_plx " val %x\n",
                (target_phys_addr_t)addr, val);
    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, 1);
}

PCI_HOST_STATIC void glue(pci_host_data_writew, PCI_HOST_SUFFIX)(
    void* opaque, PCI_ADDR_T addr, uint32_t val)
{
    PCIHostState *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    PCI_DPRINTF("writew addr " TARGET_FMT_plx " val %x\n",
                (target_phys_addr_t)addr, val);
    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, 2);
}

PCI_HOST_STATIC void glue(pci_host_data_writel, PCI_HOST_SUFFIX)(
    void* opaque, PCI_ADDR_T addr, uint32_t val)
{
    PCIHostState *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    PCI_DPRINTF("writel addr " TARGET_FMT_plx " val %x\n",
                (target_phys_addr_t)addr, val);
    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg, val, 4);
}

PCI_HOST_STATIC uint32_t glue(pci_host_data_readb, PCI_HOST_SUFFIX)(
    void* opaque, PCI_ADDR_T addr)
{
    PCIHostState *s = opaque;
    uint32_t val;

    if (!(s->config_reg & (1 << 31)))
        return 0xff;
    val = pci_data_read(s->bus, s->config_reg | (addr & 3), 1);
    PCI_DPRINTF("readb addr " TARGET_FMT_plx " val %x\n",
                (target_phys_addr_t)addr, val);
    return val;
}

PCI_HOST_STATIC uint32_t glue(pci_host_data_readw, PCI_HOST_SUFFIX)(
    void* opaque, PCI_ADDR_T addr)
{
    PCIHostState *s = opaque;
    uint32_t val;
    if (!(s->config_reg & (1 << 31)))
        return 0xffff;
    val = pci_data_read(s->bus, s->config_reg | (addr & 3), 2);
    PCI_DPRINTF("readw addr " TARGET_FMT_plx " val %x\n",
                (target_phys_addr_t)addr, val);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    return val;
}

PCI_HOST_STATIC uint32_t glue(pci_host_data_readl, PCI_HOST_SUFFIX)(
    void* opaque, PCI_ADDR_T addr)
{
    PCIHostState *s = opaque;
    uint32_t val;
    if (!(s->config_reg & (1 << 31)))
        return 0xffffffff;
    val = pci_data_read(s->bus, s->config_reg | (addr & 3), 4);
    PCI_DPRINTF("readl addr " TARGET_FMT_plx " val %x\n",
                (target_phys_addr_t)addr, val);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    return val;
}

#endif
