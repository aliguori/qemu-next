/*
 * TI OMAP3 High-Speed USB Host and OTG Controller emulation.
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "qemu-common.h"
#include "qemu-timer.h"
#include "usb.h"
#include "omap.h"
#include "irq.h"
#include "devices.h"

#define OMAP3_HSUSB_DEBUG

#ifdef OMAP3_HSUSB_DEBUG
#define TRACE(fmt,...) fprintf(stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#else
#define TRACE(...)
#endif

extern CPUReadMemoryFunc *musb_read[];
extern CPUWriteMemoryFunc *musb_write[];

struct omap3_hsusb_otg_s {
    qemu_irq mc_irq;
    qemu_irq dma_irq;
    struct musb_s *musb;
    
    uint8_t rev;
    uint16_t sysconfig;
    uint8_t interfsel;
    uint8_t simenable;
    uint8_t forcestdby;
};

static void omap3_hsusb_otg_reset(struct omap3_hsusb_otg_s *s)
{
    s->rev = 0;
    s->sysconfig = 0;
    s->interfsel = 0x1;
    s->simenable = 0;
    s->forcestdby = 1;
}

static uint32_t omap3_hsusb_otg_readb(void *opaque, target_phys_addr_t addr)
{
    struct omap3_hsusb_otg_s *s = (struct omap3_hsusb_otg_s *)opaque;
    if (addr < 0x200)
        return musb_read[0](s->musb, addr);
    if (addr < 0x400)
        return musb_read[0](s->musb, 0x20 + ((addr >> 3 ) & 0x3c));
    OMAP_BAD_REG(addr);
    return 0;
}

static uint32_t omap3_hsusb_otg_readh(void *opaque, target_phys_addr_t addr)
{
    struct omap3_hsusb_otg_s *s = (struct omap3_hsusb_otg_s *)opaque;
    if (addr < 0x200)
        return musb_read[1](s->musb, addr);
    if (addr < 0x400)
        return musb_read[1](s->musb, 0x20 + ((addr >> 3 ) & 0x3c));
    OMAP_BAD_REG(addr);
    return 0;
}

static uint32_t omap3_hsusb_otg_read(void *opaque, target_phys_addr_t addr)
{
    struct omap3_hsusb_otg_s *s = (struct omap3_hsusb_otg_s *)opaque;
    
    if (addr < 0x200)
        return musb_read[2](s->musb, addr);
    if (addr < 0x400)
        return musb_read[2](s->musb, 0x20 + ((addr >> 3 ) & 0x3c));
    
    switch (addr) {
        case 0x400: /* OTG_REVISION */
            TRACE("OTG_REVISION: 0x%08x", s->rev);
            return s->rev;
        case 0x404: /* OTG_SYSCONFIG */
            TRACE("OTG_SYSCONFIG: 0x%08x", s->sysconfig);
            return s->sysconfig;
        case 0x408: /* OTG_SYSSTATUS */
            TRACE("OTG_SYSSTATUS: 0x00000001");
            return 1; /* reset finished */
        case 0x40c: /* OTG_INTERFSEL */
            TRACE("OTG_INTERFSEL: 0x%08x", s->interfsel);
            return s->interfsel;
        case 0x410: /* OTG_SIMENABLE */
            TRACE("OTG_SIMENABLE: 0x%08x", s->simenable);
            return s->simenable;
        case 0x414: /* OTG_FORCESTDBY */
            TRACE("OTG_FORCESTDBY: 0x%08x", s->forcestdby);
            return s->forcestdby;
        default:
            break;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap3_hsusb_otg_writeb(void *opaque, target_phys_addr_t addr,
                                   uint32_t value)
{
    struct omap3_hsusb_otg_s *s = (struct omap3_hsusb_otg_s *)opaque;
    
    if (addr < 0x200)
        musb_write[0](s->musb, addr, value);
    else if (addr < 0x400)
        musb_write[0](s->musb, 0x20 + ((addr >> 3) & 0x3c), value);
    else
        OMAP_BAD_REG(addr);
}

static void omap3_hsusb_otg_writeh(void *opaque, target_phys_addr_t addr,
                                   uint32_t value)
{
    struct omap3_hsusb_otg_s *s = (struct omap3_hsusb_otg_s *)opaque;
    
    if (addr < 0x200)
        musb_write[1](s->musb, addr, value);
    else if (addr < 0x400)
        musb_write[1](s->musb, 0x20 + ((addr >> 3) & 0x3c), value);
    else
        OMAP_BAD_REG(addr);
}

static void omap3_hsusb_otg_write(void *opaque, target_phys_addr_t addr,
                                  uint32_t value)
{
    struct omap3_hsusb_otg_s *s = (struct omap3_hsusb_otg_s *)opaque;
    
    if (addr < 0x200)
        musb_write[2](s->musb, addr, value);
    else if (addr < 0x400)
        musb_write[2](s->musb, 0x20 + ((addr >> 3) & 0x3c), value);
    else switch (addr) {
        case 0x400: /* OTG_REVISION */
        case 0x408: /* OTG_SYSSTATUS */
            OMAP_RO_REGV(addr, value);
            break;
        case 0x404: /* OTG_SYSCONFIG */
            TRACE("OTG_SYSCONFIG = 0x%08x", value);
            if (value & 2) /* SOFTRESET */
                omap3_hsusb_otg_reset(s);
            s->sysconfig = value & 0x301f;
            break;
        case 0x40c: /* OTG_INTERFSEL */
            TRACE("OTG_INTERFSEL = 0x%08x", value);
            s->interfsel = value & 0x3;
            break;
        case 0x410: /* OTG_SIMENABLE */
            TRACE("OTG_SIMENABLE = 0x%08x", value);
            s->simenable = value & 1;
            break;
        case 0x414: /* OTG_FORCESTDBY */
            TRACE("OTG_FORCESTDBY = 0x%08x", value);
            s->forcestdby = value & 1;
            break;
        default:
            OMAP_BAD_REGV(addr, value);
            break;
    }
}

static CPUReadMemoryFunc *omap3_hsusb_otg_readfn[] = {
    omap3_hsusb_otg_readb,
    omap3_hsusb_otg_readh,
    omap3_hsusb_otg_read,
};

static CPUWriteMemoryFunc *omap3_hsusb_otg_writefn[] = {
    omap3_hsusb_otg_writeb,
    omap3_hsusb_otg_writeh,
    omap3_hsusb_otg_write,
};

static void omap3_hsusb_musb_core_intr(void *opaque, int source, int level)
{
    struct omap3_hsusb_otg_s *s = (struct omap3_hsusb_otg_s *)opaque;
    
    qemu_set_irq(s->mc_irq, musb_core_intr_get(s->musb));
}

static void omap3_hsusb_otg_init(struct omap_target_agent_s *otg_ta,
                                 qemu_irq mc_irq,
                                 qemu_irq dma_irq,
                                 struct omap3_hsusb_otg_s *s)
{
    s->mc_irq = mc_irq;
    s->dma_irq = dma_irq;
    
    omap_l4_attach(otg_ta, 0, l4_register_io_memory(0, omap3_hsusb_otg_readfn,
                                                    omap3_hsusb_otg_writefn, s));
    
    s->musb = musb_init(qemu_allocate_irqs(omap3_hsusb_musb_core_intr, s, __musb_irq_max));
    omap3_hsusb_otg_reset(s);
}

struct omap3_hsusb_host_s {
    struct {
        qemu_irq ohci_irq;
        qemu_irq ehci_irq;
    } hc;
    struct {
        qemu_irq irq;
    } tll;
};

static uint32_t omap3_hsusb_host_read(void *opaque, target_phys_addr_t addr)
{
    TRACE(OMAP_FMT_plx, addr);
    return 0;
}

static void omap3_hsusb_host_write(void *opaque, target_phys_addr_t addr,
                                   uint32_t value)
{
    TRACE(OMAP_FMT_plx " = 0x%08x", addr, value);
}

static CPUReadMemoryFunc *omap3_hsusb_host_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap3_hsusb_host_read,
};

static CPUWriteMemoryFunc *omap3_hsusb_host_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap3_hsusb_host_write,
};

static uint32_t omap3_hsusb_tll_read(void *opaque, target_phys_addr_t addr)
{
    TRACE(OMAP_FMT_plx, addr);
    return 0;
}

static void omap3_hsusb_tll_write(void *opaque, target_phys_addr_t addr,
                                  uint32_t value)
{
    TRACE(OMAP_FMT_plx " = 0x%08x", addr, value);
}

static CPUReadMemoryFunc *omap3_hsusb_tll_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap3_hsusb_tll_read,
};

static CPUWriteMemoryFunc *omap3_hsusb_tll_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap3_hsusb_tll_write,
};

static void omap3_hsusb_host_init(struct omap_target_agent_s *host_ta,
                                  struct omap_target_agent_s *tll_ta,
                                  qemu_irq ohci_irq,
                                  qemu_irq ehci_irq,
                                  qemu_irq tll_irq,
                                  struct omap3_hsusb_host_s *s)
{
    s->hc.ohci_irq = ohci_irq;
    s->hc.ehci_irq = ehci_irq;
    s->tll.irq     = tll_irq;
    
    omap_l4_attach(host_ta, 0, l4_register_io_memory(0, omap3_hsusb_host_readfn,
                                                     omap3_hsusb_host_writefn, s));
    omap_l4_attach(tll_ta, 0, l4_register_io_memory(0, omap3_hsusb_tll_readfn,
                                                    omap3_hsusb_tll_writefn, s));
}

struct omap3_hsusb_s {
    struct omap3_hsusb_otg_s otg;
    struct omap3_hsusb_host_s host;
};

struct omap3_hsusb_s *omap3_hsusb_init(struct omap_target_agent_s *otg_ta,
                                       struct omap_target_agent_s *host_ta,
                                       struct omap_target_agent_s *tll_ta,
                                       qemu_irq mc_irq,
                                       qemu_irq dma_irq,
                                       qemu_irq ohci_irq,
                                       qemu_irq ehci_irq,
                                       qemu_irq tll_irq)
{
    struct omap3_hsusb_s *s = qemu_mallocz(sizeof(struct omap3_hsusb_s));
    omap3_hsusb_otg_init(otg_ta, mc_irq, dma_irq, &s->otg);
    omap3_hsusb_host_init(host_ta, tll_ta, ohci_irq, ehci_irq, tll_irq, &s->host);
    return s;
}