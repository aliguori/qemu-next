/*
 * OMAP2/3 Display Subsystem.
 *
 * Copyright (C) 2008,2009 Nokia Corporation
 * OMAP2 support written by Andrzej Zaborowski <andrew@openedhand.com>
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
#include "hw.h"
#include "console.h"
#include "omap.h"
#include "qemu-common.h"
#include "sysemu.h"
#include "devices.h"
#include "vga_int.h"
#include "pixel_ops.h"

//#define OMAP_DSS_DEBUG
//#define OMAP_DSS_DEBUG_DISPC
//#define OMAP_DSS_DEBUG_DISS
#define OMAP_DSS_DEBUG_DSI
//#define OMAP_DSS_DEBUG_RFBI
//#define OMAP_DSS_DEBUG_VENC

#ifdef OMAP_DSS_DEBUG
#define TRACE(fmt,...) fprintf(stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define LAYERNAME(n) ((!(n)) ? "GFX" : ((n)==1) ? "VID1" : "VID2")
#ifdef OMAP_DSS_DEBUG_DISPC
#define TRACEDISPC(fmt,...) TRACE(fmt, ##__VA_ARGS__)
#else
#define TRACEDISPC(...)
#endif
#ifdef OMAP_DSS_DEBUG_DISS
#define TRACEDISS(fmt,...) TRACE(fmt, ##__VA_ARGS__)
#else
#define TRACEDISS(...)
#endif
#ifdef OMAP_DSS_DEBUG_DSI
#define TRACEDSI(fmt,...) TRACE(fmt, ##__VA_ARGS__)
#else
#define TRACEDSI(...)
#endif
#ifdef OMAP_DSS_DEBUG_RFBI
#define TRACERFBI(fmt,...) TRACE(fmt, ##__VA_ARGS__)
#else
#define TRACERFBI(...)
#endif
#ifdef OMAP_DSS_DEBUG_VENC
#define TRACEVENC(fmt,...) TRACE(fmt, ##__VA_ARGS__)
#else
#define TRACEVENC(...)
#endif
#else
#define TRACE(...)
#define TRACEDISPC(...)
#define TRACEDISS(...)
#define TRACEDSI(...)
#define TRACERFBI(...)
#define TRACEVENC(...)
#endif

struct omap3_lcd_panel_s {
    struct omap_dss_s *dss;
    DisplayState *state;
    omap3_lcd_panel_fn_t *line_fn_tab[2];
    omap3_lcd_panel_fn_t line_fn;
    uint32_t invalidate;
};

struct omap_dss_s {
    qemu_irq irq;
    qemu_irq drq;
    DisplayState *state;

    int autoidle;
    int control;
    uint32_t sdi_control;
    uint32_t pll_control;
    int enable;

    struct omap_dss_panel_s {
        int enable;
        int nx;
        int ny;

        int x;
        int y;
    } dig, lcd;
    struct omap3_lcd_panel_s *omap_lcd_panel[2];

    struct {
        uint8_t rev;
        uint32_t idlemode;
        uint32_t irqst;
        uint32_t irqen;
        uint32_t control;
        uint32_t config;
        uint32_t capable;
        uint32_t timing[4];
        int line;
        uint32_t bg[2];
        uint32_t trans[2];
        uint32_t global_alpha;
        uint32_t cpr_coef_r;
        uint32_t cpr_coef_g;
        uint32_t cpr_coef_b;
        
        struct omap_dss_plane_s {
            int enable;
            int bpp;
            int posx;
            int posy;
            int nx;
            int ny;

            int rotation_flag;
            int gfx_format;
            int gfx_channel;
            
            target_phys_addr_t addr[3];

            uint32_t attr;
            uint32_t tresh;
            int rowinc;
            int colinc;
            int wininc;
            
            uint32_t preload;
            
            /* following for l1 & l2 only (VID1 and VID2) */
            uint32_t fir;
            uint32_t fir_coef_h[8];
            uint32_t fir_coef_hv[8];
            uint32_t fir_coef_v[8];
            uint32_t conv_coef[5];
            uint32_t picture_size;
            uint32_t accu[2];
        } l[3];
        
        int invalidate;
        uint16_t palette[256];
    } dispc;

    struct {
        int idlemode;
        uint32_t control;
        int enable;
        int pixels;
        int busy;
        int skiplines;
        uint16_t rxbuf;
        uint32_t config[2];
        uint32_t time[4];
        uint32_t data[6];
        uint16_t vsync;
        uint16_t hsync;
        struct rfbi_chip_s *chip[2];
    } rfbi;
    
    struct {
        /* protocol engine registers */
        uint32_t sysconfig;
        uint32_t irqst;
        uint32_t irqen;
        uint32_t ctrl;
        uint32_t complexio_cfg1;
        uint32_t complexio_irqst;
        uint32_t complexio_irqen;
        uint32_t clk_ctrl;
        uint32_t timing1;
        uint32_t timing2;
        uint32_t vm_timing1;
        uint32_t vm_timing2;
        uint32_t vm_timing3;
        uint32_t clk_timing;
        uint32_t tx_fifo_vc_size;
        uint32_t rx_fifo_vc_size;
        struct {
            uint32_t ctrl;
            uint32_t te;
            uint32_t lp_header;
            uint32_t lp_payload;
            uint32_t sp_header;
            uint32_t irqst;
            uint32_t irqen;
        } vc[4];
        /* phy registers */
        uint32_t phy_cfg0;
        uint32_t phy_cfg1;
        uint32_t phy_cfg2;
        /* pll controller registers */
        uint32_t pll_control;
        uint32_t pll_go;
        uint32_t pll_config1;
        uint32_t pll_config2;
    } dsi;
};

static void omap_dss_interrupt_update(struct omap_dss_s *s)
{
    qemu_set_irq(s->irq, 
                 (s->dsi.irqst & s->dsi.irqen)
                 | (s->dsi.complexio_irqst & s->dsi.complexio_irqen)
                 | (s->dsi.vc[0].irqst & s->dsi.vc[0].irqen)
                 | (s->dsi.vc[1].irqst & s->dsi.vc[1].irqen)
                 | (s->dsi.vc[2].irqst & s->dsi.vc[2].irqen)
                 | (s->dsi.vc[3].irqst & s->dsi.vc[3].irqen)
                 | (s->dispc.irqst & s->dispc.irqen));
}

static void omap_dss_save_state(QEMUFile *f, void *opaque)
{
    struct omap_dss_s *s = (struct omap_dss_s *)opaque;
    int i, j;
    
    qemu_put_sbe32(f, s->autoidle);
    qemu_put_sbe32(f, s->control);
    qemu_put_be32(f, s->sdi_control);
    qemu_put_be32(f, s->pll_control);
    qemu_put_sbe32(f, s->enable);
    qemu_put_sbe32(f, s->dig.enable);
    qemu_put_sbe32(f, s->dig.nx);
    qemu_put_sbe32(f, s->dig.ny);
    qemu_put_sbe32(f, s->dig.x);
    qemu_put_sbe32(f, s->dig.y);
    qemu_put_sbe32(f, s->lcd.enable);
    qemu_put_sbe32(f, s->lcd.nx);
    qemu_put_sbe32(f, s->lcd.ny);
    qemu_put_sbe32(f, s->lcd.x);
    qemu_put_sbe32(f, s->lcd.y);
    qemu_put_be32(f, s->dispc.idlemode);
    qemu_put_be32(f, s->dispc.irqst);
    qemu_put_be32(f, s->dispc.irqen);
    qemu_put_be32(f, s->dispc.control);
    qemu_put_be32(f, s->dispc.config);
    qemu_put_be32(f, s->dispc.capable);
    qemu_put_be32(f, s->dispc.timing[0]);
    qemu_put_be32(f, s->dispc.timing[1]);
    qemu_put_be32(f, s->dispc.timing[2]);
    qemu_put_be32(f, s->dispc.timing[3]);
    qemu_put_sbe32(f, s->dispc.line);
    qemu_put_be32(f, s->dispc.bg[0]);
    qemu_put_be32(f, s->dispc.bg[1]);
    qemu_put_be32(f, s->dispc.trans[0]);
    qemu_put_be32(f, s->dispc.trans[1]);
    qemu_put_be32(f, s->dispc.global_alpha);
    qemu_put_be32(f, s->dispc.cpr_coef_r);
    qemu_put_be32(f, s->dispc.cpr_coef_g);
    qemu_put_be32(f, s->dispc.cpr_coef_b);
    for (i = 0; i < 3; i++) {
        qemu_put_sbe32(f, s->dispc.l[i].enable);
        qemu_put_sbe32(f, s->dispc.l[i].bpp);
        qemu_put_sbe32(f, s->dispc.l[i].posx);
        qemu_put_sbe32(f, s->dispc.l[i].posy);
        qemu_put_sbe32(f, s->dispc.l[i].nx);
        qemu_put_sbe32(f, s->dispc.l[i].ny);
        qemu_put_sbe32(f, s->dispc.l[i].rotation_flag);
        qemu_put_sbe32(f, s->dispc.l[i].gfx_format);
        qemu_put_sbe32(f, s->dispc.l[i].gfx_channel);
        for (j = 0; j < 3; j++) {
#if TARGET_PHYS_ADDR_BITS == 32
            qemu_put_be32(f, s->dispc.l[i].addr[j]);
#elif TARGET_PHYS_ADDR_BITS == 64
            qemu_put_be64(f, s->dispc.l[i].addr[j]);
#else
#error TARGET_PHYS_ADDR_BITS undefined
#endif
        }
        qemu_put_be32(f, s->dispc.l[i].attr);
        qemu_put_be32(f, s->dispc.l[i].tresh);
        qemu_put_sbe32(f, s->dispc.l[i].rowinc);
        qemu_put_sbe32(f, s->dispc.l[i].colinc);
        qemu_put_sbe32(f, s->dispc.l[i].wininc);
        qemu_put_be32(f, s->dispc.l[i].preload);
        qemu_put_be32(f, s->dispc.l[i].fir);
        for (j = 0; j < 8; j++) {
            qemu_put_be32(f, s->dispc.l[i].fir_coef_h[j]);
            qemu_put_be32(f, s->dispc.l[i].fir_coef_hv[j]);
            qemu_put_be32(f, s->dispc.l[i].fir_coef_v[j]);
            if (j < 5)
                qemu_put_be32(f, s->dispc.l[i].conv_coef[j]);
        }
        qemu_put_be32(f, s->dispc.l[i].picture_size);
        qemu_put_be32(f, s->dispc.l[i].accu[0]);
        qemu_put_be32(f, s->dispc.l[i].accu[1]);
    }
    qemu_put_sbe32(f, s->dispc.invalidate);
    for (i = 0; i < 256; i++)
        qemu_put_be16(f, s->dispc.palette[i]);
    qemu_put_sbe32(f, s->rfbi.idlemode);
    qemu_put_be32(f, s->rfbi.control);
    qemu_put_sbe32(f, s->rfbi.enable);
    qemu_put_sbe32(f, s->rfbi.pixels);
    qemu_put_sbe32(f, s->rfbi.busy);
    qemu_put_sbe32(f, s->rfbi.skiplines);
    qemu_put_be16(f, s->rfbi.rxbuf);
    for (i = 0; i < 6; i++) {
        if (i < 2)
            qemu_put_be32(f, s->rfbi.config[i]);
        if (i < 4)
            qemu_put_be32(f, s->rfbi.time[i]);
        qemu_put_be32(f, s->rfbi.data[i]);
    }
    qemu_put_be16(f, s->rfbi.vsync);
    qemu_put_be16(f, s->rfbi.hsync);
    qemu_put_be32(f, s->dsi.sysconfig);
    qemu_put_be32(f, s->dsi.irqst);
    qemu_put_be32(f, s->dsi.irqen);
    qemu_put_be32(f, s->dsi.ctrl);
    qemu_put_be32(f, s->dsi.complexio_cfg1);
    qemu_put_be32(f, s->dsi.complexio_irqst);
    qemu_put_be32(f, s->dsi.complexio_irqen);
    qemu_put_be32(f, s->dsi.clk_ctrl);
    qemu_put_be32(f, s->dsi.timing1);
    qemu_put_be32(f, s->dsi.timing2);
    qemu_put_be32(f, s->dsi.vm_timing1);
    qemu_put_be32(f, s->dsi.vm_timing2);
    qemu_put_be32(f, s->dsi.vm_timing3);
    qemu_put_be32(f, s->dsi.clk_timing);
    qemu_put_be32(f, s->dsi.tx_fifo_vc_size);
    qemu_put_be32(f, s->dsi.rx_fifo_vc_size);
    for (i = 0; i < 4; i++) {
        qemu_put_be32(f, s->dsi.vc[i].ctrl);
        qemu_put_be32(f, s->dsi.vc[i].te);
        qemu_put_be32(f, s->dsi.vc[i].lp_header);
        qemu_put_be32(f, s->dsi.vc[i].lp_payload);
        qemu_put_be32(f, s->dsi.vc[i].sp_header);
        qemu_put_be32(f, s->dsi.vc[i].irqst);
        qemu_put_be32(f, s->dsi.vc[i].irqen);
    }
    qemu_put_be32(f, s->dsi.phy_cfg0);
    qemu_put_be32(f, s->dsi.phy_cfg1);
    qemu_put_be32(f, s->dsi.phy_cfg2);
    qemu_put_be32(f, s->dsi.pll_control);
    qemu_put_be32(f, s->dsi.pll_go);
    qemu_put_be32(f, s->dsi.pll_config1);
    qemu_put_be32(f, s->dsi.pll_config2);
}

static int omap_dss_load_state(QEMUFile *f, void *opaque, int version_id)
{
    struct omap_dss_s *s = (struct omap_dss_s *)opaque;
    int i, j;
    
    if (version_id)
        return -EINVAL;
    
    s->autoidle = qemu_get_sbe32(f);
    s->control = qemu_get_sbe32(f);
    s->sdi_control = qemu_get_be32(f);
    s->pll_control = qemu_get_be32(f);
    s->enable = qemu_get_sbe32(f);
    s->dig.enable = qemu_get_sbe32(f);
    s->dig.nx = qemu_get_sbe32(f);
    s->dig.ny = qemu_get_sbe32(f);
    s->dig.x = qemu_get_sbe32(f);
    s->dig.y = qemu_get_sbe32(f);
    s->lcd.enable = qemu_get_sbe32(f);
    s->lcd.nx = qemu_get_sbe32(f);
    s->lcd.ny = qemu_get_sbe32(f);
    s->lcd.x = qemu_get_sbe32(f);
    s->lcd.y = qemu_get_sbe32(f);
    s->dispc.idlemode = qemu_get_be32(f);
    s->dispc.irqst = qemu_get_be32(f);
    s->dispc.irqen = qemu_get_be32(f);
    s->dispc.control = qemu_get_be32(f);
    s->dispc.config = qemu_get_be32(f);
    s->dispc.capable = qemu_get_be32(f);
    s->dispc.timing[0] = qemu_get_be32(f);
    s->dispc.timing[1] = qemu_get_be32(f);
    s->dispc.timing[2] = qemu_get_be32(f);
    s->dispc.timing[3] = qemu_get_be32(f);
    s->dispc.line = qemu_get_sbe32(f);
    s->dispc.bg[0] = qemu_get_be32(f);
    s->dispc.bg[1] = qemu_get_be32(f);
    s->dispc.trans[0] = qemu_get_be32(f);
    s->dispc.trans[1] = qemu_get_be32(f);
    s->dispc.global_alpha = qemu_get_be32(f);
    s->dispc.cpr_coef_r = qemu_get_be32(f);
    s->dispc.cpr_coef_g = qemu_get_be32(f);
    s->dispc.cpr_coef_b = qemu_get_be32(f);
    for (i = 0; i < 3; i++) {
        s->dispc.l[i].enable = qemu_get_sbe32(f);
        s->dispc.l[i].bpp = qemu_get_sbe32(f);
        s->dispc.l[i].posx = qemu_get_sbe32(f);
        s->dispc.l[i].posy = qemu_get_sbe32(f);
        s->dispc.l[i].nx = qemu_get_sbe32(f);
        s->dispc.l[i].ny = qemu_get_sbe32(f);
        s->dispc.l[i].rotation_flag = qemu_get_sbe32(f);
        s->dispc.l[i].gfx_format = qemu_get_sbe32(f);
        s->dispc.l[i].gfx_channel = qemu_get_sbe32(f);
        for (j = 0; j < 3; j++) {
#if TARGET_PHYS_ADDR_BITS == 32
            s->dispc.l[i].addr[j] = qemu_get_be32(f);
#elif TARGET_PHYS_ADDR_BITS == 64
            s->dispc.l[i].addr[j] = qemu_get_be64(f);
#else
#error TARGET_PHYS_ADDR_BITS undefined
#endif
        }
        s->dispc.l[i].attr = qemu_get_be32(f);
        s->dispc.l[i].tresh = qemu_get_be32(f);
        s->dispc.l[i].rowinc = qemu_get_sbe32(f);
        s->dispc.l[i].colinc = qemu_get_sbe32(f);
        s->dispc.l[i].wininc = qemu_get_sbe32(f);
        s->dispc.l[i].preload = qemu_get_be32(f);
        s->dispc.l[i].fir = qemu_get_be32(f);
        for (j = 0; j < 8; j++) {
            s->dispc.l[i].fir_coef_h[j] = qemu_get_be32(f);
            s->dispc.l[i].fir_coef_hv[j] = qemu_get_be32(f);
            s->dispc.l[i].fir_coef_v[j] = qemu_get_be32(f);
            if (j < 5)
                s->dispc.l[i].conv_coef[j] = qemu_get_be32(f);
        }
        s->dispc.l[i].picture_size = qemu_get_be32(f);
        s->dispc.l[i].accu[0] = qemu_get_be32(f);
        s->dispc.l[i].accu[1] = qemu_get_be32(f);
    }
    s->dispc.invalidate = qemu_get_sbe32(f);
    for (i = 0; i < 256; i++)
        s->dispc.palette[i] = qemu_get_be16(f);
    s->rfbi.idlemode = qemu_get_sbe32(f);
    s->rfbi.control = qemu_get_be32(f);
    s->rfbi.enable = qemu_get_sbe32(f);
    s->rfbi.pixels = qemu_get_sbe32(f);
    s->rfbi.busy = qemu_get_sbe32(f);
    s->rfbi.skiplines = qemu_get_sbe32(f);
    s->rfbi.rxbuf = qemu_get_be16(f);
    for (i = 0; i < 6; i++) {
        if (i < 2)
            s->rfbi.config[i] = qemu_get_be32(f);
        if (i < 4)
            s->rfbi.time[i] = qemu_get_be32(f);
        s->rfbi.data[i] = qemu_get_be32(f);
    }
    s->rfbi.vsync = qemu_get_be16(f);
    s->rfbi.hsync = qemu_get_be16(f);
    s->dsi.sysconfig = qemu_get_be32(f);
    s->dsi.irqst = qemu_get_be32(f);
    s->dsi.irqen = qemu_get_be32(f);
    s->dsi.ctrl = qemu_get_be32(f);
    s->dsi.complexio_cfg1 = qemu_get_be32(f);
    s->dsi.complexio_irqst = qemu_get_be32(f);
    s->dsi.complexio_irqen = qemu_get_be32(f);
    s->dsi.clk_ctrl = qemu_get_be32(f);
    s->dsi.timing1 = qemu_get_be32(f);
    s->dsi.timing2 = qemu_get_be32(f);
    s->dsi.vm_timing1 = qemu_get_be32(f);
    s->dsi.vm_timing2 = qemu_get_be32(f);
    s->dsi.vm_timing3 = qemu_get_be32(f);
    s->dsi.clk_timing = qemu_get_be32(f);
    s->dsi.tx_fifo_vc_size = qemu_get_be32(f);
    s->dsi.rx_fifo_vc_size = qemu_get_be32(f);
    for (i = 0; i < 4; i++) {
        s->dsi.vc[i].ctrl = qemu_get_be32(f);
        s->dsi.vc[i].te = qemu_get_be32(f);
        s->dsi.vc[i].lp_header = qemu_get_be32(f);
        s->dsi.vc[i].lp_payload = qemu_get_be32(f);
        s->dsi.vc[i].sp_header = qemu_get_be32(f);
        s->dsi.vc[i].irqst = qemu_get_be32(f);
        s->dsi.vc[i].irqen = qemu_get_be32(f);
    }
    s->dsi.phy_cfg0 = qemu_get_be32(f);
    s->dsi.phy_cfg1 = qemu_get_be32(f);
    s->dsi.phy_cfg2 = qemu_get_be32(f);
    s->dsi.pll_control = qemu_get_be32(f);
    s->dsi.pll_go = qemu_get_be32(f);
    s->dsi.pll_config1 = qemu_get_be32(f);
    s->dsi.pll_config2 = qemu_get_be32(f);
    
    s->dispc.invalidate = 1; /* force refresh of display parameters */
    if (s->omap_lcd_panel[0])
        s->omap_lcd_panel[0]->invalidate = 1;
    if (s->omap_lcd_panel[1])
        s->omap_lcd_panel[1]->invalidate = 1;
    
    omap_dss_interrupt_update(s);

    return 0;
}

static void omap_dsi_reset(struct omap_dss_s *s)
{
    bzero(&s->dsi, sizeof(s->dsi));
    s->dsi.sysconfig = 0x11;
    s->dsi.ctrl = 0x100;
    s->dsi.complexio_cfg1 = 0x20000000;
    s->dsi.timing1 = 0x7fff7fff;
    s->dsi.timing2 = 0x7fff7fff;
    s->dsi.clk_timing = 0x0101;
    s->dsi.phy_cfg0 = 0x1a3c1a28;
    s->dsi.phy_cfg1 = 0x420a1875;
    s->dsi.phy_cfg2 = 0xb800001b;
}

static void omap_rfbi_reset(struct omap_dss_s *s)
{
    s->rfbi.idlemode = 0;
    s->rfbi.control = 2;
    s->rfbi.enable = 0;
    s->rfbi.pixels = 0;
    s->rfbi.skiplines = 0;
    s->rfbi.busy = 0;
    s->rfbi.config[0] = 0x00310000;
    s->rfbi.config[1] = 0x00310000;
    s->rfbi.time[0] = 0;
    s->rfbi.time[1] = 0;
    s->rfbi.time[2] = 0;
    s->rfbi.time[3] = 0;
    s->rfbi.data[0] = 0;
    s->rfbi.data[1] = 0;
    s->rfbi.data[2] = 0;
    s->rfbi.data[3] = 0;
    s->rfbi.data[4] = 0;
    s->rfbi.data[5] = 0;
    s->rfbi.vsync = 0;
    s->rfbi.hsync = 0;
}

void omap_dss_reset(struct omap_dss_s *s)
{
    int i, j;
    
    s->autoidle = 0x10; /* was 0 for OMAP2 but bit4 must be set for OMAP3 */
    s->control = 0;
    s->sdi_control = 0;
    s->pll_control = 0;
    s->enable = 0;

    s->dig.enable = 0;
    s->dig.nx = 1;
    s->dig.ny = 1;

    s->lcd.enable = 0;
    s->lcd.nx = 1;
    s->lcd.ny = 1;

    s->dispc.idlemode = 0;
    s->dispc.irqst = 0;
    s->dispc.irqen = 0;
    s->dispc.control = 0;
    s->dispc.config = 0;
    s->dispc.capable = 0x161;
    s->dispc.timing[0] = 0;
    s->dispc.timing[1] = 0;
    s->dispc.timing[2] = 0;
    s->dispc.timing[3] = 0x00010002;
    s->dispc.line = 0;
    s->dispc.bg[0] = 0;
    s->dispc.bg[1] = 0;
    s->dispc.trans[0] = 0;
    s->dispc.trans[1] = 0;
    s->dispc.global_alpha = 0;
    s->dispc.cpr_coef_r = 0;
    s->dispc.cpr_coef_g = 0;
    s->dispc.cpr_coef_b = 0;

    for (i = 0; i < 3; i++) {
        s->dispc.l[i].enable = 0;
        s->dispc.l[i].bpp = 0;
        s->dispc.l[i].addr[0] = 0;
        s->dispc.l[i].addr[1] = 0;
        s->dispc.l[i].addr[2] = 0;
        s->dispc.l[i].posx = 0;
        s->dispc.l[i].posy = 0;
        s->dispc.l[i].nx = 1;
        s->dispc.l[i].ny = 1;
        s->dispc.l[i].attr = 0;
        s->dispc.l[i].tresh = (s->dispc.rev < 0x30) ? 0 : 0x03ff03c0;
        s->dispc.l[i].rowinc = 1;
        s->dispc.l[i].colinc = 1;
        s->dispc.l[i].wininc = 0;
        s->dispc.l[i].preload = 0x100;
        s->dispc.l[i].fir = 0;
        s->dispc.l[i].picture_size = 0;
        s->dispc.l[i].accu[0] = 0;
        s->dispc.l[i].accu[1] = 0;
        for (j = 0; j < 5; j++)
            s->dispc.l[i].conv_coef[j] = 0;
        for (j = 0; j < 8; j++) {
            s->dispc.l[i].fir_coef_h[j] = 0;
            s->dispc.l[i].fir_coef_hv[j] = 0;
            s->dispc.l[i].fir_coef_v[j] = 0;
        }
    }
        
    omap_dsi_reset(s);
    omap_rfbi_reset(s);
    omap_dss_interrupt_update(s);
}

static uint32_t omap_diss_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_dss_s *s = (struct omap_dss_s *) opaque;


    switch (addr) {
    case 0x00:	/* DSS_REVISIONNUMBER */
        TRACEDISS("DSS_REVISIONNUMBER: 0x20");
        return 0x20;

    case 0x10:	/* DSS_SYSCONFIG */
        TRACEDISS("DSS_SYSCONFIG: 0x%08x", s->autoidle);
        return s->autoidle;

    case 0x14:	/* DSS_SYSSTATUS */
        TRACEDISS("DSS_SYSSTATUS: 0x1");
        return 1;						/* RESETDONE */
            
    case 0x18:  /* DSS_IRQSTATUS */
        TRACEDISS("DSS_IRQSTATUS: 0x%08x",
                 ((s->dsi.irqst & s->dsi.irqen) ? 2 : 0) 
                 | ((s->dispc.irqst & s->dispc.irqen) ? 1 : 0));
        return ((s->dsi.irqst & s->dsi.irqen) ? 2 : 0) 
            | ((s->dispc.irqst & s->dispc.irqen) ? 1 : 0);
            
    case 0x40:	/* DSS_CONTROL */
        TRACEDISS("DSS_CONTROL: 0x%08x", s->control);
        return s->control;

    case 0x44:  /* DSS_SDI_CONTROL */
        TRACEDISS("DSS_SDI_CONTROL: 0x%08x", s->sdi_control);
        return s->sdi_control;
            
    case 0x48: /* DSS_PLL_CONTROL */
        TRACEDISS("DSS_PLL_CONTROL: 0x%08x", s->pll_control);
        return s->pll_control;

    case 0x50:	/* DSS_PSA_LCD_REG_1 */
    case 0x54:	/* DSS_PSA_LCD_REG_2 */
    case 0x58:	/* DSS_PSA_VIDEO_REG */
        TRACEDISS("DSS_PSA_xxx: 0");
        /* TODO: fake some values when appropriate s->control bits are set */
        return 0;

    case 0x5c:	/* DSS_SDI_STATUS */
        /* TODO: check and implement missing OMAP3 bits */
        TRACEDISS("DSS_STATUS: 0x%08x", 1 + (s->control & 1));
        return 1 + (s->control & 1);

    default:
        break;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_diss_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_dss_s *s = (struct omap_dss_s *) opaque;

    switch (addr) {
    case 0x00:	/* DSS_REVISIONNUMBER */
    case 0x14:	/* DSS_SYSSTATUS */
    case 0x18:  /* DSS_IRQSTATUS */
    case 0x50:	/* DSS_PSA_LCD_REG_1 */
    case 0x54:	/* DSS_PSA_LCD_REG_2 */
    case 0x58:	/* DSS_PSA_VIDEO_REG */
    case 0x5c:	/* DSS_STATUS */
        /* quietly ignore */
        /*OMAP_RO_REGV(addr, value);*/
        break;

    case 0x10:	/* DSS_SYSCONFIG */
        TRACEDISS("DSS_SYSCONFIG = 0x%08x", value);
        if (value & 2)						/* SOFTRESET */
            omap_dss_reset(s);
        s->autoidle = value & 0x19; /* was 0x01 for OMAP2 */
        break;

    case 0x40:	/* DSS_CONTROL */
        TRACEDISS("DSS_CONTROL = 0x%08x", value);
        s->control = value & 0x3ff; /* was 0x3dd for OMAP2 */
        break;

    case 0x44: /* DSS_SDI_CONTROL */
        TRACEDISS("DSS_SDI_CONTROL = 0x%08x", value);
        s->sdi_control = value & 0x000ff80f;
        break;

    case 0x48: /* DSS_PLL_CONTROL */
        TRACEDISS("DSS_PLL_CONTROL = 0x%08x", value);
        s->pll_control = value;
        break;
            
    default:
        OMAP_BAD_REGV(addr, value);
        break;
    }
}

static CPUReadMemoryFunc *omap_diss1_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_diss_read,
};

static CPUWriteMemoryFunc *omap_diss1_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_diss_write,
};

static uint32_t omap_disc_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_dss_s *s = (struct omap_dss_s *) opaque;
    int n = 0;

    switch (addr) {
    case 0x000:	/* DISPC_REVISION */
        TRACEDISPC("DISPC_REVISION: 0x%08x", s->dispc.rev);
        return s->dispc.rev;
    case 0x010:	/* DISPC_SYSCONFIG */
        TRACEDISPC("DISPC_SYSCONFIG: 0x%08x", s->dispc.idlemode);
        return s->dispc.idlemode;
    case 0x014:	/* DISPC_SYSSTATUS */
        TRACEDISPC("DISPC_SYSSTATUS: 1");
        return 1;						/* RESETDONE */
    case 0x018:	/* DISPC_IRQSTATUS */
        TRACEDISPC("DISPC_IRQSTATUS: 0x%08x", s->dispc.irqst);
        return s->dispc.irqst;
    case 0x01c:	/* DISPC_IRQENABLE */
        TRACEDISPC("DISPC_IRQENABLE: 0x%08x", s->dispc.irqen);
        return s->dispc.irqen;
    case 0x040:	/* DISPC_CONTROL */
        TRACEDISPC("DISPC_CONTROL: 0x%08x", s->dispc.control);
        return s->dispc.control;
    case 0x044:	/* DISPC_CONFIG */
        TRACEDISPC("DISPC_CONFIG: 0x%08x", s->dispc.config);
        return s->dispc.config;
    case 0x048:	/* DISPC_CAPABLE */
        TRACEDISPC("DISPC_CAPABLE: 0x%08x", s->dispc.capable);
        return s->dispc.capable;
    case 0x04c:	/* DISPC_DEFAULT_COLOR0 */
        TRACEDISPC("DISPC_DEFAULT_COLOR0: 0x%08x", s->dispc.bg[0]);
        return s->dispc.bg[0];
    case 0x050:	/* DISPC_DEFAULT_COLOR1 */
        TRACEDISPC("DISPC_DEFAULT_COLOR0: 0x%08x", s->dispc.bg[1]);
        return s->dispc.bg[1];
    case 0x054:	/* DISPC_TRANS_COLOR0 */
        TRACEDISPC("DISPC_TRANS_COLOR0: 0x%08x", s->dispc.trans[0]);
        return s->dispc.trans[0];
    case 0x058:	/* DISPC_TRANS_COLOR1 */
        TRACEDISPC("DISPC_TRANS_COLOR0: 0x%08x", s->dispc.trans[1]);
        return s->dispc.trans[1];
    case 0x05c:	/* DISPC_LINE_STATUS */
        TRACEDISPC("DISPC_LINE_STATUS: 0x7ff");
        return 0x7ff;
    case 0x060:	/* DISPC_LINE_NUMBER */
        TRACEDISPC("DISPC_LINE_NUMBER: 0x%08x", s->dispc.line);
        return s->dispc.line;
    case 0x064:	/* DISPC_TIMING_H */
        TRACEDISPC("DISPC_TIMING_H: 0x%08x", s->dispc.timing[0]);
        return s->dispc.timing[0];
    case 0x068:	/* DISPC_TIMING_V */
        TRACEDISPC("DISPC_TIMING_H: 0x%08x", s->dispc.timing[1]);
        return s->dispc.timing[1];
    case 0x06c:	/* DISPC_POL_FREQ */
        TRACEDISPC("DISPC_POL_FREQ: 0x%08x", s->dispc.timing[2]);
        return s->dispc.timing[2];
    case 0x070:	/* DISPC_DIVISOR */
        TRACEDISPC("DISPC_DIVISOR: 0x%08x", s->dispc.timing[3]);
        return s->dispc.timing[3];
    case 0x074: /* DISPC_GLOBAL_ALPHA */
        TRACEDISPC("DISPC_GLOBAL_ALPHA: 0x%08x", s->dispc.global_alpha);
        return s->dispc.global_alpha;
    case 0x078:	/* DISPC_SIZE_DIG */
        TRACEDISPC("DISPC_SIZE_DIG: 0x%08x", ((s->dig.ny - 1) << 16) | (s->dig.nx - 1));
        return ((s->dig.ny - 1) << 16) | (s->dig.nx - 1);
    case 0x07c:	/* DISPC_SIZE_LCD */
        TRACEDISPC("DISPC_SIZE_LCD: 0x%08x", ((s->lcd.ny - 1) << 16) | (s->lcd.nx - 1));
        return ((s->lcd.ny - 1) << 16) | (s->lcd.nx - 1);
    case 0x14c:	/* DISPC_VID2_BA0 */
        n++;
    case 0x0bc:	/* DISPC_VID1_BA0 */
        n++;
    case 0x080:	/* DISPC_GFX_BA0 */
        TRACEDISPC("DISPC_%s_BA0: " OMAP_FMT_plx, LAYERNAME(n), s->dispc.l[n].addr[0]);
        return s->dispc.l[n].addr[0];
    case 0x150:	/* DISPC_VID2_BA1 */
        n++;
    case 0x0c0:	/* DISPC_VID1_BA1 */
        n++;
    case 0x084:	/* DISPC_GFX_BA1 */
        TRACEDISPC("DISPC_%s_BA1: " OMAP_FMT_plx, LAYERNAME(n), s->dispc.l[n].addr[1]);
        return s->dispc.l[n].addr[1];
    case 0x154:	/* DISPC_VID2_POSITION */
        n++;
    case 0x0c4:	/* DISPC_VID1_POSITION */
        n++;
    case 0x088:	/* DISPC_GFX_POSITION */
        TRACEDISPC("DISPC_%s_POSITION: 0x%08x", LAYERNAME(n),
                 (s->dispc.l[n].posy << 16) | s->dispc.l[n].posx);
        return (s->dispc.l[n].posy << 16) | s->dispc.l[n].posx;
    case 0x158:	/* DISPC_VID2_SIZE */
        n++;
    case 0x0c8:	/* DISPC_VID1_SIZE */
        n++;
    case 0x08c:	/* DISPC_GFX_SIZE */
        TRACEDISPC("DISPC_%s_SIZE: 0x%08x", LAYERNAME(n),
                 ((s->dispc.l[n].ny - 1) << 16) | (s->dispc.l[n].nx - 1));
        return ((s->dispc.l[n].ny - 1) << 16) | (s->dispc.l[n].nx - 1);
    case 0x15c:	/* DISPC_VID2_ATTRIBUTES */
        n++;
    case 0x0cc:	/* DISPC_VID1_ATTRIBUTES */
        n++;
    case 0x0a0:	/* DISPC_GFX_ATTRIBUTES */
        TRACEDISPC("DISPC_%s_ATTRIBUTES: 0x%08x", LAYERNAME(n),
                 s->dispc.l[n].attr);
        return s->dispc.l[n].attr;
    case 0x160:	/* DISPC_VID2_FIFO_THRESHOLD */
        n++;
    case 0x0d0:	/* DISPC_VID1_FIFO_THRESHOLD */
        n++;
    case 0x0a4:	/* DISPC_GFX_FIFO_TRESHOLD */
        TRACEDISPC("DISPC_%s_THRESHOLD: 0x%08x", LAYERNAME(n),
                 s->dispc.l[n].tresh);
        return s->dispc.l[n].tresh;
    case 0x164:	/* DISPC_VID2_FIFO_SIZE_STATUS */
        n++;
    case 0x0d4:	/* DISPC_VID1_FIFO_SIZE_STATUS */
        n++;
    case 0x0a8:	/* DISPC_GFX_FIFO_SIZE_STATUS */
        TRACEDISPC("DISPC_%s_FIFO_SIZE_STATUS: 0x%08x", LAYERNAME(n),
                 s->dispc.rev < 0x30 ? 256 : 1024);
        return s->dispc.rev < 0x30 ? 256 : 1024;
    case 0x168:	/* DISPC_VID2_ROW_INC */
        n++;
    case 0x0d8:	/* DISPC_VID1_ROW_INC */
        n++;
    case 0x0ac:	/* DISPC_GFX_ROW_INC */
        TRACEDISPC("DISPC_%s_ROW_INC: 0x%08x", LAYERNAME(n),
                 s->dispc.l[n].rowinc);
        return s->dispc.l[n].rowinc;
    case 0x16c:	/* DISPC_VID2_PIXEL_INC */
        n++;
    case 0x0dc:	/* DISPC_VID1_PIXEL_INC */
        n++;
    case 0x0b0:	/* DISPC_GFX_PIXEL_INC */
        TRACEDISPC("DISPC_%s_PIXEL_INC: 0x%08x", LAYERNAME(n),
                 s->dispc.l[n].colinc);
        return s->dispc.l[n].colinc;
    case 0x0b4:	/* DISPC_GFX_WINDOW_SKIP */
        TRACEDISPC("DISPC_GFX_WINDOW_SKIP: 0x%08x", s->dispc.l[0].wininc);
        return s->dispc.l[0].wininc;
    case 0x0b8:	/* DISPC_GFX_TABLE_BA */
        TRACEDISPC("DISPC_GFX_TABLE_BA: " OMAP_FMT_plx, s->dispc.l[0].addr[2]);
        return s->dispc.l[0].addr[2];
    case 0x170:	/* DISPC_VID2_FIR */
        n++;
    case 0x0e0:	/* DISPC_VID1_FIR */
        n++;
        TRACEDISPC("DISPC_%s_FIR: 0x%08x", LAYERNAME(n),
                 s->dispc.l[n].fir);
        return s->dispc.l[n].fir;
    case 0x174:	/* DISPC_VID2_PICTURE_SIZE */
        n++;
    case 0x0e4:	/* DISPC_VID1_PICTURE_SIZE */
        n++;
        TRACEDISPC("DISPC_%s_PICTURE_SIZE: 0x%08x", LAYERNAME(n),
                 s->dispc.l[n].picture_size);
        return s->dispc.l[n].picture_size;
    case 0x178:	/* DISPC_VID2_ACCU0 */
    case 0x17c:	/* DISPC_VID2_ACCU1 */
        n++;
    case 0x0e8:	/* DISPC_VID1_ACCU0 */
    case 0x0ec:	/* DISPC_VID1_ACCU1 */
        n++;
        TRACEDISPC("DISPC_%s_ACCU%d: 0x%08x", LAYERNAME(n),
                 (int)((addr >> 1) & 1), s->dispc.l[n].accu[(addr >> 1 ) & 1]);
        return s->dispc.l[n].accu[(addr >> 1) & 1];
    case 0x180 ... 0x1bc:	/* DISPC_VID2_FIR_COEF */
        n++;
    case 0x0f0 ... 0x12c:	/* DISPC_VID1_FIR_COEF */
        n++;
        if (addr & 4) {
            TRACEDISPC("DISPC_%s_FIR_COEF_HV%d: 0x%08x", LAYERNAME(n),
                     (int)((addr - ((n > 1) ? 0x180 : 0xf0)) / 8),
                     s->dispc.l[n].fir_coef_hv[(addr - ((n > 1) ? 0x180 : 0xf0)) / 8]);
            return s->dispc.l[n].fir_coef_hv[(addr - ((n > 1) ? 0x180 : 0xf0)) / 8];
        }
        TRACEDISPC("DISPC_%s_FIR_COEF_H%d: 0x%08x", LAYERNAME(n),
                 (int)((addr - ((n > 1) ? 0x180 : 0xf0)) / 8),
                 s->dispc.l[n].fir_coef_h[(addr - ((n > 1) ? 0x180 : 0xf0)) / 8]);
        return s->dispc.l[n].fir_coef_h[(addr - ((n > 1) ? 0x180 : 0xf0)) / 8];
    case 0x1c0 ... 0x1d0: /* DISPC_VID2_CONV_COEFi */
        n++;
    case 0x130 ... 0x140: /* DISPC_VID1_CONV_COEFi */
        n++;
        TRACEDISPC("DISPC_%s_CONV_COEF%d: 0x%08x", LAYERNAME(n),
                 (int)((addr - ((n > 1) ? 0x1c0 : 0x130)) / 4),
                 s->dispc.l[n].conv_coef[(addr - ((n > 1) ? 0x1c0 : 0x130)) / 4]);
        return s->dispc.l[n].conv_coef[(addr - ((n > 1) ? 0x1c0 : 0x130)) / 4];
    case 0x1d4:	/* DISPC_DATA_CYCLE1 */
    case 0x1d8:	/* DISPC_DATA_CYCLE2 */
    case 0x1dc:	/* DISPC_DATA_CYCLE3 */
        TRACEDISPC("DISPC_DATA_CYCLE%d: 0", (int)((addr - 0x1d4) / 4));
        return 0;
    case 0x200 ... 0x21c: /* DISPC_VID2_FIR_COEF_Vi */
        n++;
    case 0x1e0 ... 0x1fc: /* DISPC_VID1_FIR_COEF_Vi */
        n++;
        TRACEDISPC("DISPC_%s_FIR_COEF_V%d: 0x%08x", LAYERNAME(n),
                 (int)((addr & 0x01f) / 4),
                 s->dispc.l[n].fir_coef_v[(addr & 0x01f) / 4]);
        return s->dispc.l[n].fir_coef_v[(addr & 0x01f) / 4];
    case 0x220: /* DISPC_CPR_COEF_R */
        TRACEDISPC("DISPC_CPR_COEF_R: 0x%08x", s->dispc.cpr_coef_r);
        return s->dispc.cpr_coef_r;
    case 0x224: /* DISPC_CPR_COEF_G */
        TRACEDISPC("DISPC_CPR_COEF_G: 0x%08x", s->dispc.cpr_coef_g);
        return s->dispc.cpr_coef_g;
    case 0x228: /* DISPC_CPR_COEF_B */
        TRACEDISPC("DISPC_CPR_COEF_B: 0x%08x", s->dispc.cpr_coef_b);
        return s->dispc.cpr_coef_b;
    case 0x234: /* DISPC_VID2_PRELOAD */
        n++;
    case 0x230: /* DISPC_VID1_PRELOAD */
        n++;
    case 0x22c: /* DISPC_GFX_PRELOAD */
        TRACEDISPC("DISPC_%s_PRELOAD: 0x%08x", LAYERNAME(n),
                 s->dispc.l[n].preload);
        return s->dispc.l[n].preload;
    default:
        break;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_disc_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_dss_s *s = (struct omap_dss_s *) opaque;
    int n = 0;

    switch (addr) {
    case 0x000: /* DISPC_REVISION */
    case 0x014: /* DISPC_SYSSTATUS */
    case 0x05c: /* DISPC_LINE_STATUS */
    case 0x0a8: /* DISPC_GFX_FIFO_SIZE_STATUS */
        /* quietly ignore */
        /*OMAP_RO_REGV(addr, value);*/
        break;
    case 0x010:	/* DISPC_SYSCONFIG */
        TRACEDISPC("DISPC_SYSCONFIG = 0x%08x", value);
        if (value & 2)						/* SOFTRESET */
            omap_dss_reset(s);
        s->dispc.idlemode = value & ((s->dispc.rev < 0x30) ? 0x301b : 0x331f);
        break;
    case 0x018:	/* DISPC_IRQSTATUS */
        TRACEDISPC("DISPC_IRQSTATUS = 0x%08x", value);
        s->dispc.irqst &= ~value;
        omap_dss_interrupt_update(s);
        break;
    case 0x01c:	/* DISPC_IRQENABLE */
        TRACEDISPC("DISPC_IRQENABLE = 0x%08x", value);
        s->dispc.irqen = value & ((s->dispc.rev < 0x30) ? 0xffff : 0x1ffff);
        omap_dss_interrupt_update(s);
        break;
    case 0x040:	/* DISPC_CONTROL */
        TRACEDISPC("DISPC_CONTROL = 0x%08x", value);
        if (s->dispc.rev < 0x30)
            s->dispc.control = value & 0x07ff9fff;
        else
            s->dispc.control = (value & 0xffff9bff) | (s->dispc.control & 0x6000);
        s->dig.enable = (value >> 1) & 1;
        s->lcd.enable = (value >> 0) & 1;
        if (value & (1 << 12))			/* OVERLAY_OPTIMIZATION */
            if (~((s->dispc.l[1].attr | s->dispc.l[2].attr) & 1))
                 fprintf(stderr, "%s: Overlay Optimization when no overlay "
                                 "region effectively exists leads to "
                                 "unpredictable behaviour!\n", __FUNCTION__);
        if (value & (1 << 6)) {				/* GODIGITAL */
            /* XXX: Shadowed fields are:
             * s->dispc.config
             * s->dispc.capable
             * s->dispc.bg[0]
             * s->dispc.bg[1]
             * s->dispc.trans[0]
             * s->dispc.trans[1]
             * s->dispc.line
             * s->dispc.timing[0]
             * s->dispc.timing[1]
             * s->dispc.timing[2]
             * s->dispc.timing[3]
             * s->lcd.nx
             * s->lcd.ny
             * s->dig.nx
             * s->dig.ny
             * s->dispc.l[0].addr[0]
             * s->dispc.l[0].addr[1]
             * s->dispc.l[0].addr[2]
             * s->dispc.l[0].posx
             * s->dispc.l[0].posy
             * s->dispc.l[0].nx
             * s->dispc.l[0].ny
             * s->dispc.l[0].tresh
             * s->dispc.l[0].rowinc
             * s->dispc.l[0].colinc
             * s->dispc.l[0].wininc
             * All they need to be loaded here from their shadow registers.
             */
            s->dispc.control &= ~(1 << 6); /* GODIGITAL finished */
        }
        if (value & (1 << 5)) {				/* GOLCD */
             /* XXX: Likewise for LCD here.  */
            s->dispc.control &= ~(1 << 5); /* GOLCD finished */
        }
        s->dispc.invalidate = 1;
        break;
    case 0x044:	/* DISPC_CONFIG */
        TRACEDISPC("DISPC_CONFIG = 0x%08x", value);
        s->dispc.config = value & 0x3fff;
        /* XXX:
         * bits 2:1 (LOADMODE) reset to 0 after set to 1 and palette loaded
         * bits 2:1 (LOADMODE) reset to 2 after set to 3 and palette loaded
         */
        s->dispc.invalidate = 1;
        break;
    case 0x048:	/* DISPC_CAPABLE */
        TRACEDISPC("DISPC_CAPABLE = 0x%08x", value);
        s->dispc.capable = value & 0x3ff;
        break;
    case 0x04c:	/* DISPC_DEFAULT_COLOR0 */
        TRACEDISPC("DISPC_DEFAULT_COLOR0 = 0x%08x", value);
        s->dispc.bg[0] = value & 0xffffff;
        s->dispc.invalidate = 1;
        break;
    case 0x050:	/* DISPC_DEFAULT_COLOR1 */
        TRACEDISPC("DISPC_DEFAULT_COLOR1 = 0x%08x", value);
        s->dispc.bg[1] = value & 0xffffff;
        s->dispc.invalidate = 1;
        break;
    case 0x054:	/* DISPC_TRANS_COLOR0 */
        TRACEDISPC("DISPC_TRANS_COLOR0 = 0x%08x", value);
        s->dispc.trans[0] = value & 0xffffff;
        s->dispc.invalidate = 1;
        break;
    case 0x058:	/* DISPC_TRANS_COLOR1 */
        TRACEDISPC("DISPC_TRANS_COLOR1 = 0x%08x", value);
        s->dispc.trans[1] = value & 0xffffff;
        s->dispc.invalidate = 1;
        break;
    case 0x060:	/* DISPC_LINE_NUMBER */
        TRACEDISPC("DISPC_LINE_NUMBER = 0x%08x", value);
        s->dispc.line = value & 0x7ff;
        break;
    case 0x064:	/* DISPC_TIMING_H */
        TRACEDISPC("DISPC_TIMING_H = 0x%08x", value);
        s->dispc.timing[0] = value & 0x0ff0ff3f;
        break;
    case 0x068:	/* DISPC_TIMING_V */
        TRACEDISPC("DISPC_TIMING_V = 0x%08x", value);
        s->dispc.timing[1] = value & 0x0ff0ff3f;
        break;
    case 0x06c:	/* DISPC_POL_FREQ */
        TRACEDISPC("DISPC_POL_FREQ = 0x%08x", value);
        s->dispc.timing[2] = value & 0x0003ffff;
        break;
    case 0x070:	/* DISPC_DIVISOR */
        TRACEDISPC("DISPC_DIVISOR = 0x%08x", value);
        s->dispc.timing[3] = value & 0x00ff00ff;
        break;
    case 0x074: /* DISPC_GLOBAL_ALPHA */
        TRACEDISPC("DISPC_GLOBAL_ALPHA = 0x%08x", value);
        s->dispc.global_alpha = value & 0x00ff00ff;
        break;
    case 0x078:	/* DISPC_SIZE_DIG */
        s->dig.nx = ((value >>  0) & 0x7ff) + 1;		/* PPL */
        s->dig.ny = ((value >> 16) & 0x7ff) + 1;		/* LPP */
        s->dispc.invalidate = 1;
        TRACEDISPC("DISPC_SIZE_DIG = 0x%08x (%dx%d)", value, s->dig.nx, s->dig.ny);
        break;
    case 0x07c:	/* DISPC_SIZE_LCD */
        s->lcd.nx = ((value >>  0) & 0x7ff) + 1;		/* PPL */
        s->lcd.ny = ((value >> 16) & 0x7ff) + 1;		/* LPP */
        s->dispc.invalidate = 1;
        TRACEDISPC("DISPC_SIZE_LCD = 0x%08x (%dx%d)", value, s->lcd.nx, s->lcd.ny);
        break;
    case 0x14c:	/* DISPC_VID2_BA0 */
        n++;
    case 0x0bc: /* DISPC_VID1_BA0 */
        n++;
    case 0x080:	/* DISPC_GFX_BA0 */
        TRACEDISPC("DISPC_%s_BA0 = 0x%08x", LAYERNAME(n), value);
        s->dispc.l[n].addr[0] = (target_phys_addr_t) value;
        s->dispc.invalidate = 1;
        break;
    case 0x150:	/* DISPC_VID2_BA1 */
        n++;
    case 0x0c0:	/* DISPC_VID1_BA1 */
        n++;
    case 0x084:	/* DISPC_GFX_BA1 */
        TRACEDISPC("DISPC_%s_BA1 = 0x%08x", LAYERNAME(n), value);
        s->dispc.l[n].addr[1] = (target_phys_addr_t) value;
        s->dispc.invalidate = 1;
        break;
    case 0x154:	/* DISPC_VID2_POSITION */
        n++;
    case 0x0c4:	/* DISPC_VID1_POSITION */
        n++;
    case 0x088:	/* DISPC_GFX_POSITION */
        s->dispc.l[n].posx = ((value >>  0) & 0x7ff);		/* GFXPOSX */
        s->dispc.l[n].posy = ((value >> 16) & 0x7ff);		/* GFXPOSY */
        s->dispc.invalidate = 1;
        TRACEDISPC("DISPC_%s_POSITION = 0x%08x (%d,%d)", LAYERNAME(n),
                 value, s->dispc.l[n].posx, s->dispc.l[n].posy);
        break;
    case 0x158:	/* DISPC_VID2_SIZE */
        n++;
    case 0x0c8:	/* DISPC_VID1_SIZE */
        n++;
    case 0x08c:	/* DISPC_GFX_SIZE */
        s->dispc.l[n].nx = ((value >>  0) & 0x7ff) + 1;		/* GFXSIZEX */
        s->dispc.l[n].ny = ((value >> 16) & 0x7ff) + 1;		/* GFXSIZEY */
        TRACEDISPC("DISPC_%s_SIZE = 0x%08x (%dx%d)", LAYERNAME(n),
                 value, s->dispc.l[n].nx, s->dispc.l[n].ny);
        s->dispc.invalidate = 1;
        break;
        n++;
    case 0x0a0:	/* DISPC_GFX_ATTRIBUTES */
        TRACEDISPC("DISPC_GFX_ATTRIBUTES = 0x%08x", value);
        s->dispc.l[0].attr = value & 0xffff;
        if (value & (3 << 9))
            fprintf(stderr, "%s: Big-endian pixel format not supported\n",
                            __FUNCTION__);
        s->dispc.l[0].enable = value & 1;
        s->dispc.l[0].bpp = (value >> 1) & 0xf;
        s->dispc.l[0].rotation_flag = (value >> 12) & 0x3;
        s->dispc.l[0].gfx_format = (value >> 1) & 0xf;
        s->dispc.l[0].gfx_channel = (value >> 8) & 0x1;
        s->dispc.invalidate = 1;
        break;
    case 0x160:	/* DISPC_VID2_FIFO_TRESHOLD */
        n++;
    case 0x0d0:	/* DISPC_VID1_FIFO_TRESHOLD */
        n++;
    case 0x0a4:	/* DISPC_GFX_FIFO_THRESHOLD */
        TRACEDISPC("DISPC_%s_FIFO_THRESHOLD = 0x%08x", LAYERNAME(n), value);
        s->dispc.l[n].tresh = value & ((s->dispc.rev < 0x30) 
                                       ? 0x01ff01ff : 0x0fff0fff);
        break;
    case 0x168:	/* DISPC_VID2_ROW_INC */
        n++;
    case 0x0d8:	/* DISPC_VID1_ROW_INC */
        n++;
    case 0x0ac:	/* DISPC_GFX_ROW_INC */
        TRACEDISPC("DISPC_%s_ROW_INC = 0x%08x", LAYERNAME(n), value);
        s->dispc.l[n].rowinc = value;
        s->dispc.invalidate = 1;
        break;
    case 0x16c:	/* DISPC_VID2_PIXEL_INC */
        n++;
    case 0x0dc:	/* DISPC_VID1_PIXEL_INC */
        n++;
    case 0x0b0:	/* DISPC_GFX_PIXEL_INC */
        TRACEDISPC("DISPC_%s_PIXEL_INC = 0x%08x", LAYERNAME(n), value);
        s->dispc.l[n].colinc = value;
        s->dispc.invalidate = 1;
        break;
    case 0x0b4:	/* DISPC_GFX_WINDOW_SKIP */
        TRACEDISPC("DISPC_GFX_WINDOW_SKIP = 0x%08x", value);
        s->dispc.l[0].wininc = value;
        break;
    case 0x0b8:	/* DISPC_GFX_TABLE_BA */
        TRACEDISPC("DISPC_GFX_TABLE_BA = 0x%08x", value);
        s->dispc.l[0].addr[2] = (target_phys_addr_t) value;
        s->dispc.invalidate = 1;
        break;
    case 0x15c:	/* DISPC_VID2_ATTRIBUTES */
        n++;
    case 0x0cc:	/* DISPC_VID1_ATTRIBUTES */
        n++;
        TRACEDISPC("DISPC_%s_ATTRIBUTES = 0x%08x", LAYERNAME(n), value);
        s->dispc.l[n].attr = value & 0x1fffffff;
        break;
    case 0x170:	/* DISPC_VID2_FIR */
        n++;
    case 0x0e0:	/* DISPC_VID1_FIR */
        n++;
        TRACEDISPC("DISPC_%s_FIR = 0x%08x", LAYERNAME(n), value);
        s->dispc.l[n].fir = value & 0x1fff1fff;
        break;
    case 0x174:	/* DISPC_VID2_PICTURE_SIZE */
        n++;
    case 0x0e4:	/* DISPC_VID1_PICTURE_SIZE */
        n++;
        TRACEDISPC("DISPC_%s_PICTURE_SIZE = 0x%08x", LAYERNAME(n), value);
        s->dispc.l[n].picture_size = value & 0x07ff07ff;
        break;
    case 0x178:	/* DISPC_VID2_ACCU0 */
    case 0x17c:	/* DISPC_VID2_ACCU1 */
        n++;
    case 0x0e8:	/* DISPC_VID1_ACCU0 */
    case 0x0ec:	/* DISPC_VID1_ACCU1 */
        n++;
        TRACEDISPC("DISPC_%s_ACCU%d = 0x%08x", LAYERNAME(n),
                 (int)((addr >> 1) & 1), value);
        s->dispc.l[n].accu[(addr >> 1) & 1] = value & 0x03ff03ff;
        break;
    case 0x180 ... 0x1bc:	/* DISPC_VID2_FIR_COEF */
        n++;
    case 0x0f0 ... 0x12c:	/* DISPC_VID1_FIR_COEF */
        n++;
        if (addr & 4) {
            TRACEDISPC("DISPC_%s_FIR_COEF_HV%d = 0x%08x", LAYERNAME(n),
                     (int)((addr - ((n > 1) ? 0x180 : 0xf0)) / 8), value);
            s->dispc.l[n].fir_coef_hv[(addr - ((n > 1) ? 0x180 : 0xf0)) / 8] = value;
        } else {
            TRACEDISPC("DISPC_%s_FIR_COEF_H%d = 0x%08x", LAYERNAME(n),
                     (int)((addr - ((n > 1) ? 0x180 : 0xf0)) / 8), value);
            s->dispc.l[n].fir_coef_h[(addr - ((n > 1) ? 0x180 : 0xf0)) / 8] = value;
        }
        break;
    case 0x1c0 ... 0x1d0: /* DISPC_VID2_CONV_COEFi */
        n++;
    case 0x130 ... 0x140: /* DISPC_VID1_CONV_COEFi */
        n++;
        TRACEDISPC("DISPC_%s_CONV_COEF%d = 0x%08x", LAYERNAME(n),
                 (int)((addr - ((n > 1) ? 0x1c0 : 0x130)) / 4), value);
        s->dispc.l[n].conv_coef[(addr - ((n > 1) ? 0x1c0 : 0x130)) / 4] = value;
        break;
    case 0x1d4:	/* DISPC_DATA_CYCLE1 */
    case 0x1d8:	/* DISPC_DATA_CYCLE2 */
    case 0x1dc:	/* DISPC_DATA_CYCLE3 */
        TRACEDISPC("DISPC_DATA_CYCLE%d = 0x%08x (ignored)",
                 (int)((addr - 0x1d4) / 4), value);
        break;
    case 0x200 ... 0x21c: /* DISPC_VID2_FIR_COEF_Vi */
        n++;
    case 0x1e0 ... 0x1fc: /* DISPC_VID1_FIR_COEF_Vi */
        n++;
        TRACEDISPC("DISPC_%s_FIR_COEF_V%d = 0x%08x", LAYERNAME(n),
                 (int)((addr & 0x01f) / 4), value);
        s->dispc.l[n].fir_coef_v[(addr & 0x01f) / 4] = value & 0x0000ffff;
        break;
    case 0x220: /* DISPC_CPR_COEF_R */
        TRACEDISPC("DISPC_CPR_COEF_R = 0x%08x", value);
        s->dispc.cpr_coef_r = value & 0xffbffbff;
        break;
    case 0x224: /* DISPC_CPR_COEF_G */
        TRACEDISPC("DISPC_CPR_COEF_G = 0x%08x", value);
        s->dispc.cpr_coef_g = value & 0xffbffbff;
        break;
    case 0x228: /* DISPC_CPR_COEF_B */
        TRACEDISPC("DISPC_CPR_COEF_B = 0x%08x", value);
        s->dispc.cpr_coef_b = value & 0xffbffbff;
        break;
    case 0x234: /* DISPC_VID2_PRELOAD */
        n++;
    case 0x230: /* DISPC_VID1_PRELOAD */
        n++;
    case 0x22c: /* DISPC_GFX_PRELOAD */
        TRACEDISPC("DISPC_%s_PRELOAD = 0x%08x", LAYERNAME(n), value);
        s->dispc.l[n].preload = value & 0x0fff;
        break;
    default:
        OMAP_BAD_REGV(addr, value);
        break;
    }
}

static CPUReadMemoryFunc *omap_disc1_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_disc_read,
};

static CPUWriteMemoryFunc *omap_disc1_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_disc_write,
};

static void omap_rfbi_transfer_stop(struct omap_dss_s *s)
{
    if (!s->rfbi.busy)
        return;

    /* TODO: in non-Bypass mode we probably need to just deassert the DRQ.  */

    s->rfbi.busy = 0;
}

static void omap_rfbi_transfer_start(struct omap_dss_s *s)
{
    void *data;
    target_phys_addr_t len;
    target_phys_addr_t data_addr;
    int pitch;
    static void *bounce_buffer;
    static target_phys_addr_t bounce_len;

    if (!s->rfbi.enable || s->rfbi.busy)
        return;

    if (s->rfbi.control & (1 << 1)) {				/* BYPASS */
        /* TODO: in non-Bypass mode we probably need to just assert the
         * DRQ and wait for DMA to write the pixels.  */
        fprintf(stderr, "%s: Bypass mode unimplemented\n", __FUNCTION__);
        return;
    }

    if (!(s->dispc.control & (1 << 11)))			/* RFBIMODE */
        return;
    /* TODO: check that LCD output is enabled in DISPC.  */

    s->rfbi.busy = 1;

    len = s->rfbi.pixels * 2;

    data_addr = s->dispc.l[0].addr[0];
    data = cpu_physical_memory_map(data_addr, &len, 0);
    if (data && len != s->rfbi.pixels * 2) {
        cpu_physical_memory_unmap(data, len, 0, 0);
        data = NULL;
        len = s->rfbi.pixels * 2;
    }
    if (!data) {
        if (len > bounce_len) {
            bounce_buffer = qemu_realloc(bounce_buffer, len);
        }
        data = bounce_buffer;
        cpu_physical_memory_read(data_addr, data, len);
    }

    /* TODO bpp */
    s->rfbi.pixels = 0;

    /* TODO: negative values */
    pitch = s->dispc.l[0].nx + (s->dispc.l[0].rowinc - 1) / 2;

    if ((s->rfbi.control & (1 << 2)) && s->rfbi.chip[0])
        s->rfbi.chip[0]->block(s->rfbi.chip[0]->opaque, 1, data, len, pitch);
    if ((s->rfbi.control & (1 << 3)) && s->rfbi.chip[1])
        s->rfbi.chip[1]->block(s->rfbi.chip[1]->opaque, 1, data, len, pitch);

    if (data != bounce_buffer) {
        cpu_physical_memory_unmap(data, len, 0, len);
    }

    omap_rfbi_transfer_stop(s);

    /* TODO */
    s->dispc.irqst |= 1;					/* FRAMEDONE */
    omap_dss_interrupt_update(s);
}

static uint32_t omap_rfbi_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_dss_s *s = (struct omap_dss_s *) opaque;

    switch (addr) {
    case 0x00:	/* RFBI_REVISION */
        TRACERFBI("RFBI_REVISION: 0x10");
        return 0x10;

    case 0x10:	/* RFBI_SYSCONFIG */
        TRACERFBI("RFBI_SYSCONFIG: 0x%08x", s->rfbi.idlemode);
        return s->rfbi.idlemode;

    case 0x14:	/* RFBI_SYSSTATUS */
        TRACERFBI("RFBI_SYSSTATUS: 0x%08x", 1 | (s->rfbi.busy << 8));
        return 1 | (s->rfbi.busy << 8);				/* RESETDONE */

    case 0x40:	/* RFBI_CONTROL */
        TRACERFBI("RFBI_CONTROL: 0x%08x", s->rfbi.control);
        return s->rfbi.control;

    case 0x44:	/* RFBI_PIXELCNT */
        TRACERFBI("RFBI_PIXELCNT: 0x%08x", s->rfbi.pixels);
        return s->rfbi.pixels;

    case 0x48:	/* RFBI_LINE_NUMBER */
        TRACERFBI("RFBI_LINE_NUMBER: 0x%08x", s->rfbi.skiplines);
        return s->rfbi.skiplines;

    case 0x58:	/* RFBI_READ */
    case 0x5c:	/* RFBI_STATUS */
        TRACERFBI("RFBI_READ/STATUS: 0x%08x", s->rfbi.rxbuf);
        return s->rfbi.rxbuf;

    case 0x60:	/* RFBI_CONFIG0 */
        TRACERFBI("RFBI_CONFIG0: 0x%08x", s->rfbi.config[0]);
        return s->rfbi.config[0];
    case 0x64:	/* RFBI_ONOFF_TIME0 */
        TRACERFBI("RFBI_ONOFF_TIME0: 0x%08x", s->rfbi.time[0]);
        return s->rfbi.time[0];
    case 0x68:	/* RFBI_CYCLE_TIME0 */
        TRACERFBI("RFBI_CYCLE_TIME0: 0x%08x", s->rfbi.time[1]);
        return s->rfbi.time[1];
    case 0x6c:	/* RFBI_DATA_CYCLE1_0 */
        TRACERFBI("RFBI_DATA_CYCLE1_0: 0x%08x", s->rfbi.data[0]);
        return s->rfbi.data[0];
    case 0x70:	/* RFBI_DATA_CYCLE2_0 */
        TRACERFBI("RFBI_DATA_CYCLE2_0: 0x%08x", s->rfbi.data[1]);
        return s->rfbi.data[1];
    case 0x74:	/* RFBI_DATA_CYCLE3_0 */
        TRACERFBI("RFBI_DATA_CYCLE3_0: 0x%08x", s->rfbi.data[2]);
        return s->rfbi.data[2];

    case 0x78:	/* RFBI_CONFIG1 */
        TRACERFBI("RFBI_CONFIG1: 0x%08x", s->rfbi.config[1]);
        return s->rfbi.config[1];
    case 0x7c:	/* RFBI_ONOFF_TIME1 */
        TRACERFBI("RFBI_ONOFF_TIME1: 0x%08x", s->rfbi.time[2]);
        return s->rfbi.time[2];
    case 0x80:	/* RFBI_CYCLE_TIME1 */
        TRACERFBI("RFBI_CYCLE_TIME1: 0x%08x", s->rfbi.time[3]);
        return s->rfbi.time[3];
    case 0x84:	/* RFBI_DATA_CYCLE1_1 */
        TRACERFBI("RFBI_DATA_CYCLE1_1: 0x%08x", s->rfbi.data[3]);
        return s->rfbi.data[3];
    case 0x88:	/* RFBI_DATA_CYCLE2_1 */
        TRACERFBI("RFBI_DATA_CYCLE2_1: 0x%08x", s->rfbi.data[4]);
        return s->rfbi.data[4];
    case 0x8c:	/* RFBI_DATA_CYCLE3_1 */
        TRACERFBI("RFBI_DATA_CYCLE3_1: 0x%08x", s->rfbi.data[5]);
        return s->rfbi.data[5];

    case 0x90:	/* RFBI_VSYNC_WIDTH */
        TRACERFBI("RFBI_VSYNC_WIDTH: 0x%08x", s->rfbi.vsync);
        return s->rfbi.vsync;
    case 0x94:	/* RFBI_HSYNC_WIDTH */
        TRACERFBI("RFBI_HSYNC_WIDTH: 0x%08x", s->rfbi.hsync);
        return s->rfbi.hsync;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_rfbi_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_dss_s *s = (struct omap_dss_s *) opaque;

    switch (addr) {
    case 0x10:	/* RFBI_SYSCONFIG */
        TRACERFBI("RFBI_SYSCONFIG = 0x%08x", value);
        if (value & 2)						/* SOFTRESET */
            omap_rfbi_reset(s);
        s->rfbi.idlemode = value & 0x19;
        break;

    case 0x40:	/* RFBI_CONTROL */
        TRACERFBI("RFBI_CONTROL = 0x%08x", value);
        s->rfbi.control = value & 0xf;
        s->rfbi.enable = value & 1;
        if (value & (1 << 4) &&					/* ITE */
                        !(s->rfbi.config[0] & s->rfbi.config[1] & 0xc))
            omap_rfbi_transfer_start(s);
        break;

    case 0x44:	/* RFBI_PIXELCNT */
        TRACERFBI("RFBI_PIXELCNT = 0x%08x", value);
        s->rfbi.pixels = value;
        break;

    case 0x48:	/* RFBI_LINE_NUMBER */
        TRACERFBI("RFBI_LINE_NUMBER = 0x%08x", value);
        s->rfbi.skiplines = value & 0x7ff;
        break;

    case 0x4c:	/* RFBI_CMD */
        TRACERFBI("RFBI_CMD = 0x%08x", value);
        if ((s->rfbi.control & (1 << 2)) && s->rfbi.chip[0])
            s->rfbi.chip[0]->write(s->rfbi.chip[0]->opaque, 0, value & 0xffff);
        if ((s->rfbi.control & (1 << 3)) && s->rfbi.chip[1])
            s->rfbi.chip[1]->write(s->rfbi.chip[1]->opaque, 0, value & 0xffff);
        break;
    case 0x50:	/* RFBI_PARAM */
        TRACERFBI("RFBI_PARAM = 0x%08x", value);
        if ((s->rfbi.control & (1 << 2)) && s->rfbi.chip[0])
            s->rfbi.chip[0]->write(s->rfbi.chip[0]->opaque, 1, value & 0xffff);
        if ((s->rfbi.control & (1 << 3)) && s->rfbi.chip[1])
            s->rfbi.chip[1]->write(s->rfbi.chip[1]->opaque, 1, value & 0xffff);
        break;
    case 0x54:	/* RFBI_DATA */
        TRACERFBI("RFBI_DATA = 0x%08x", value);
        /* TODO: take into account the format set up in s->rfbi.config[?] and
         * s->rfbi.data[?], but special-case the most usual scenario so that
         * speed doesn't suffer.  */
        if ((s->rfbi.control & (1 << 2)) && s->rfbi.chip[0]) {
            s->rfbi.chip[0]->write(s->rfbi.chip[0]->opaque, 1, value & 0xffff);
            s->rfbi.chip[0]->write(s->rfbi.chip[0]->opaque, 1, value >> 16);
        }
        if ((s->rfbi.control & (1 << 3)) && s->rfbi.chip[1]) {
            s->rfbi.chip[1]->write(s->rfbi.chip[1]->opaque, 1, value & 0xffff);
            s->rfbi.chip[1]->write(s->rfbi.chip[1]->opaque, 1, value >> 16);
        }
        if (!-- s->rfbi.pixels)
            omap_rfbi_transfer_stop(s);
        break;
    case 0x58:	/* RFBI_READ */
        TRACERFBI("RFBI_READ = 0x%08x", value);
        if ((s->rfbi.control & (1 << 2)) && s->rfbi.chip[0])
            s->rfbi.rxbuf = s->rfbi.chip[0]->read(s->rfbi.chip[0]->opaque, 1);
        else if ((s->rfbi.control & (1 << 3)) && s->rfbi.chip[1])
            s->rfbi.rxbuf = s->rfbi.chip[0]->read(s->rfbi.chip[0]->opaque, 1);
        if (!-- s->rfbi.pixels)
            omap_rfbi_transfer_stop(s);
        break;

    case 0x5c:	/* RFBI_STATUS */
        TRACERFBI("RFBI_STATUS = 0x%08x", value);
        if ((s->rfbi.control & (1 << 2)) && s->rfbi.chip[0])
            s->rfbi.rxbuf = s->rfbi.chip[0]->read(s->rfbi.chip[0]->opaque, 0);
        else if ((s->rfbi.control & (1 << 3)) && s->rfbi.chip[1])
            s->rfbi.rxbuf = s->rfbi.chip[0]->read(s->rfbi.chip[0]->opaque, 0);
        if (!-- s->rfbi.pixels)
            omap_rfbi_transfer_stop(s);
        break;

    case 0x60:	/* RFBI_CONFIG0 */
        TRACERFBI("RFBI_CONFIG0 = 0x%08x", value);
        s->rfbi.config[0] = value & 0x003f1fff;
        break;

    case 0x64:	/* RFBI_ONOFF_TIME0 */
        TRACERFBI("RFBI_ONOFF_TIME0 = 0x%08x", value);
        s->rfbi.time[0] = value & 0x3fffffff;
        break;
    case 0x68:	/* RFBI_CYCLE_TIME0 */
        TRACERFBI("RFBI_CYCLE_TIME0 = 0x%08x", value);
        s->rfbi.time[1] = value & 0x0fffffff;
        break;
    case 0x6c:	/* RFBI_DATA_CYCLE1_0 */
        TRACERFBI("RFBI_DATA_CYCLE1_0 = 0x%08x", value);
        s->rfbi.data[0] = value & 0x0f1f0f1f;
        break;
    case 0x70:	/* RFBI_DATA_CYCLE2_0 */
        TRACERFBI("RFBI_DATA_CYCLE2_0 = 0x%08x", value);
        s->rfbi.data[1] = value & 0x0f1f0f1f;
        break;
    case 0x74:	/* RFBI_DATA_CYCLE3_0 */
        TRACERFBI("RFBI_DATA_CYCLE3_0 = 0x%08x", value);
        s->rfbi.data[2] = value & 0x0f1f0f1f;
        break;
    case 0x78:	/* RFBI_CONFIG1 */
        TRACERFBI("RFBI_CONFIG1 = 0x%08x", value);
        s->rfbi.config[1] = value & 0x003f1fff;
        break;

    case 0x7c:	/* RFBI_ONOFF_TIME1 */
        TRACERFBI("RFBI_ONOFF_TIME1 = 0x%08x", value);
        s->rfbi.time[2] = value & 0x3fffffff;
        break;
    case 0x80:	/* RFBI_CYCLE_TIME1 */
        TRACERFBI("RFBI_CYCLE_TIME1 = 0x%08x", value);
        s->rfbi.time[3] = value & 0x0fffffff;
        break;
    case 0x84:	/* RFBI_DATA_CYCLE1_1 */
        TRACERFBI("RFBI_DATA_CYCLE1_1 = 0x%08x", value);
        s->rfbi.data[3] = value & 0x0f1f0f1f;
        break;
    case 0x88:	/* RFBI_DATA_CYCLE2_1 */
        TRACERFBI("RFBI_DATA_CYCLE2_1 = 0x%08x", value);
        s->rfbi.data[4] = value & 0x0f1f0f1f;
        break;
    case 0x8c:	/* RFBI_DATA_CYCLE3_1 */
        TRACERFBI("RFBI_DATA_CYCLE3_1 = 0x%08x", value);
        s->rfbi.data[5] = value & 0x0f1f0f1f;
        break;

    case 0x90:	/* RFBI_VSYNC_WIDTH */
        TRACERFBI("RFBI_VSYNC_WIDTH = 0x%08x", value);
        s->rfbi.vsync = value & 0xffff;
        break;
    case 0x94:	/* RFBI_HSYNC_WIDTH */
        TRACERFBI("RFBI_HSYNC_WIDTH = 0x%08x", value);
        s->rfbi.hsync = value & 0xffff;
        break;

    default:
        OMAP_BAD_REGV(addr, value);
        break;
    }
}

static CPUReadMemoryFunc *omap_rfbi1_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_rfbi_read,
};

static CPUWriteMemoryFunc *omap_rfbi1_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_rfbi_write,
};

static uint32_t omap_venc_read(void *opaque, target_phys_addr_t addr)
{
    switch (addr) {
        case 0x00:	/* REV_ID */
            return 0x2;
        case 0x04:	/* STATUS */
        case 0x08:	/* F_CONTROL */
        case 0x10:	/* VIDOUT_CTRL */
        case 0x14:	/* SYNC_CTRL */
        case 0x1c:	/* LLEN */
        case 0x20:	/* FLENS */
        case 0x24:	/* HFLTR_CTRL */
        case 0x28:	/* CC_CARR_WSS_CARR */
        case 0x2c:	/* C_PHASE */
        case 0x30:	/* GAIN_U */
        case 0x34:	/* GAIN_V */
        case 0x38:	/* GAIN_Y */
        case 0x3c:	/* BLACK_LEVEL */
        case 0x40:	/* BLANK_LEVEL */
        case 0x44:	/* X_COLOR */
        case 0x48:	/* M_CONTROL */
        case 0x4c:	/* BSTAMP_WSS_DATA */
        case 0x50:	/* S_CARR */
        case 0x54:	/* LINE21 */
        case 0x58:	/* LN_SEL */
        case 0x5c:	/* L21__WC_CTL */
        case 0x60:	/* HTRIGGER_VTRIGGER */
        case 0x64:	/* SAVID__EAVID */
        case 0x68:	/* FLEN__FAL */
        case 0x6c:	/* LAL__PHASE_RESET */
        case 0x70:	/* HS_INT_START_STOP_X */
        case 0x74:	/* HS_EXT_START_STOP_X */
        case 0x78:	/* VS_INT_START_X */
        case 0x7c:	/* VS_INT_STOP_X__VS_INT_START_Y */
        case 0x80:	/* VS_INT_STOP_Y__VS_INT_START_X */
        case 0x84:	/* VS_EXT_STOP_X__VS_EXT_START_Y */
        case 0x88:	/* VS_EXT_STOP_Y */
        case 0x90:	/* AVID_START_STOP_X */
        case 0x94:	/* AVID_START_STOP_Y */
        case 0xa0:	/* FID_INT_START_X__FID_INT_START_Y */
        case 0xa4:	/* FID_INT_OFFSET_Y__FID_EXT_START_X */
        case 0xa8:	/* FID_EXT_START_Y__FID_EXT_OFFSET_Y */
        case 0xb0:	/* TVDETGP_INT_START_STOP_X */
        case 0xb4:	/* TVDETGP_INT_START_STOP_Y */
        case 0xb8:	/* GEN_CTRL */
        case 0xc4:	/* DAC_TST__DAC_A */
        case 0xc8:	/* DAC_B__DAC_C */
            return 0;
            
        default:
            break;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_venc_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    switch (addr) {
        case 0x00: /* REV_ID */
        case 0x04: /* STATUS */
            /* read-only, ignore */
            break;
        case 0x08:	/* F_CONTROL */
        case 0x10:	/* VIDOUT_CTRL */
        case 0x14:	/* SYNC_CTRL */
        case 0x1c:	/* LLEN */
        case 0x20:	/* FLENS */
        case 0x24:	/* HFLTR_CTRL */
        case 0x28:	/* CC_CARR_WSS_CARR */
        case 0x2c:	/* C_PHASE */
        case 0x30:	/* GAIN_U */
        case 0x34:	/* GAIN_V */
        case 0x38:	/* GAIN_Y */
        case 0x3c:	/* BLACK_LEVEL */
        case 0x40:	/* BLANK_LEVEL */
        case 0x44:	/* X_COLOR */
        case 0x48:	/* M_CONTROL */
        case 0x4c:	/* BSTAMP_WSS_DATA */
        case 0x50:	/* S_CARR */
        case 0x54:	/* LINE21 */
        case 0x58:	/* LN_SEL */
        case 0x5c:	/* L21__WC_CTL */
        case 0x60:	/* HTRIGGER_VTRIGGER */
        case 0x64:	/* SAVID__EAVID */
        case 0x68:	/* FLEN__FAL */
        case 0x6c:	/* LAL__PHASE_RESET */
        case 0x70:	/* HS_INT_START_STOP_X */
        case 0x74:	/* HS_EXT_START_STOP_X */
        case 0x78:	/* VS_INT_START_X */
        case 0x7c:	/* VS_INT_STOP_X__VS_INT_START_Y */
        case 0x80:	/* VS_INT_STOP_Y__VS_INT_START_X */
        case 0x84:	/* VS_EXT_STOP_X__VS_EXT_START_Y */
        case 0x88:	/* VS_EXT_STOP_Y */
        case 0x90:	/* AVID_START_STOP_X */
        case 0x94:	/* AVID_START_STOP_Y */
        case 0xa0:	/* FID_INT_START_X__FID_INT_START_Y */
        case 0xa4:	/* FID_INT_OFFSET_Y__FID_EXT_START_X */
        case 0xa8:	/* FID_EXT_START_Y__FID_EXT_OFFSET_Y */
        case 0xb0:	/* TVDETGP_INT_START_STOP_X */
        case 0xb4:	/* TVDETGP_INT_START_STOP_Y */
        case 0xb8:	/* GEN_CTRL */
        case 0xc4:	/* DAC_TST__DAC_A */
        case 0xc8:	/* DAC_B__DAC_C */
            break;
            
        default:
            OMAP_BAD_REGV(addr, value);
            break;
    }
}

static CPUReadMemoryFunc *omap_venc1_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_venc_read,
};

static CPUWriteMemoryFunc *omap_venc1_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_venc_write,
};

static uint32_t omap_im3_read(void *opaque, target_phys_addr_t addr)
{
    switch (addr) {
    case 0x0a8:	/* SBIMERRLOGA */
    case 0x0b0:	/* SBIMERRLOG */
    case 0x190:	/* SBIMSTATE */
    case 0x198:	/* SBTMSTATE_L */
    case 0x19c:	/* SBTMSTATE_H */
    case 0x1a8:	/* SBIMCONFIG_L */
    case 0x1ac:	/* SBIMCONFIG_H */
    case 0x1f8:	/* SBID_L */
    case 0x1fc:	/* SBID_H */
        return 0;

    default:
        break;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_im3_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    switch (addr) {
    case 0x0b0:	/* SBIMERRLOG */
    case 0x190:	/* SBIMSTATE */
    case 0x198:	/* SBTMSTATE_L */
    case 0x19c:	/* SBTMSTATE_H */
    case 0x1a8:	/* SBIMCONFIG_L */
    case 0x1ac:	/* SBIMCONFIG_H */
        break;

    default:
        OMAP_BAD_REGV(addr, value);
        break;
    }
}

static CPUReadMemoryFunc *omap_im3_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_im3_read,
};

static CPUWriteMemoryFunc *omap_im3_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_im3_write,
};

static uint32_t omap_dsi_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_dss_s *s = (struct omap_dss_s *)opaque;
    uint32_t x;
    
    switch (addr) {
        case 0x000: /* DSI_REVISION */
            TRACEDSI("DSI_REVISION = 0x10");
            return 0x10;
        case 0x010: /* DSI_SYSCONFIG */
            TRACEDSI("DSI_SYSCONFIG = 0x%04x", s->dsi.sysconfig);
            return s->dsi.sysconfig;
        case 0x014: /* DSI_SYSSTATUS */
            TRACEDSI("DSI_SYSSTATUS = 0x01");
            return 1; /* RESET_DONE */
        case 0x018: /* DSI_IRQSTATUS */
            TRACEDSI("DSI_IRQSTATUS = 0x%08x", s->dsi.irqst);
            return s->dsi.irqst;
        case 0x01c: /* DSI_IRQENABLE */
            TRACEDSI("DSI_IRQENABLE = 0x%08x", s->dsi.irqen);
            return s->dsi.irqen;
        case 0x040: /* DSI_CTRL */
            TRACEDSI("DSI_CTRL = 0x%08x", s->dsi.ctrl);
            return s->dsi.ctrl;
        case 0x048: /* DSI_COMPLEXIO_CFG1 */
            TRACEDSI("DSI_COMPLEXIO_CFG1 = 0x%08x", s->dsi.complexio_cfg1);
            return s->dsi.complexio_cfg1;
        case 0x04c: /* DSI_COMPLEXIO_IRQSTATUS */
            TRACEDSI("DSI_COMPLEXIO_IRQSTATUS = 0x%08x", s->dsi.complexio_irqst);
            return s->dsi.complexio_irqst;
        case 0x050: /* DSI_COMPLEXIO_IRQENABLE */
            TRACEDSI("DSI_COMPLEXIO_IRQENABLE = 0x%08x", s->dsi.complexio_irqen);
            return s->dsi.complexio_irqen;
        case 0x054: /* DSI_CLK_CTRL */
            TRACEDSI("DSI_CLK_CTRL = 0x%08x", s->dsi.clk_ctrl);
            return s->dsi.clk_ctrl;
        case 0x058: /* DSI_TIMING1 */
            TRACEDSI("DSI_TIMING1 = 0x%08x", s->dsi.timing1);
            return s->dsi.timing1;
        case 0x05c: /* DSI_TIMING2 */
            TRACEDSI("DSI_TIMING2 = 0x%08x", s->dsi.timing2);
            return s->dsi.timing2;
        case 0x060: /* DSI_VM_TIMING1 */
            TRACEDSI("DSI_VM_TIMING1 = 0x%08x", s->dsi.vm_timing1);
            return s->dsi.vm_timing1;
        case 0x064: /* DSI_VM_TIMING2 */
            TRACEDSI("DSI_VM_TIMING2 = 0x%08x", s->dsi.vm_timing2);
            return s->dsi.vm_timing2;
        case 0x068: /* DSI_VM_TIMING3 */
            TRACEDSI("DSI_VM_TIMING3 = 0x%08x", s->dsi.vm_timing3);
            return s->dsi.vm_timing3;
        case 0x06c: /* DSI_CLK_TIMING */
            TRACEDSI("DSI_CLK_TIMING = 0x%08x", s->dsi.clk_timing);
            return s->dsi.clk_timing;
        case 0x070: /* DSI_TX_FIFO_VC_SIZE */
            TRACEDSI("DSI_TX_FIFO_VC_SIZE = 0x%08x", s->dsi.tx_fifo_vc_size);
            return s->dsi.tx_fifo_vc_size;
        case 0x074: /* DSI_RX_FIFO_VC_SIZE */
            TRACEDSI("DSI_RX_FIFO_VC_SIZE = 0x%08x", s->dsi.rx_fifo_vc_size);
            return s->dsi.rx_fifo_vc_size;
        case 0x078: /* DSI_COMPLEXIO_CFG_2 */
        case 0x07c: /* DSI_RX_FIFO_VC_FULLNESS */
        case 0x080: /* DSI_VM_TIMING4 */
        case 0x084: /* DSI_TX_FIFO_VC_EMPTINESS */
        case 0x088: /* DSI_VM_TIMING5 */
        case 0x08c: /* DSI_VM_TIMING6 */
        case 0x090: /* DSI_VM_TIMING7 */
        case 0x094: /* DSI_STOPCLK_TIMING */
            OMAP_BAD_REG(addr);
            break;
        case 0x100 ... 0x17c: /* DSI_VCx_xxx */
            x = (addr >> 6) & 3;
            switch (addr & 0x1f) {
                case 0x00: /* DSI_VCx_CTRL */
                    TRACEDSI("DSI_VC%d_CTRL = 0x%08x", x, s->dsi.vc[x].ctrl);
                    return s->dsi.vc[x].ctrl;
                case 0x04: /* DSI_VCx_TE */
                    TRACEDSI("DSI_VC%d_TE = 0x%08x", x, s->dsi.vc[x].te);
                    return s->dsi.vc[x].te;
                case 0x08: /* DSI_VCx_LONG_PACKET_HEADER */
                    /* write-only */
                    TRACEDSI("DSI_VC%d_LONG_PACKET_HEADER = 0", x);
                    return 0;
                case 0x0c: /* DSI_VCx_LONG_PACKET_PAYLOAD */
                    /* write-only */
                    TRACEDSI("DSI_VC%d_LONG_PACKET_PAYLOAD = 0", x);
                    return 0;
                case 0x10: /* DSI_VCx_SHORT_PACKET_HEADER */
                    /* TODO: this should return value from RX FIFO */
                    TRACEDSI("DSI_VC%d_SHORT_PACKET_HEADER = 0", x);
                    return 0;
                case 0x18: /* DSI_VCx_IRQSTATUS */
                    TRACEDSI("DSI_VC%d_IRQSTATUS = 0x%08x", x, s->dsi.vc[x].irqst);
                    return s->dsi.vc[x].irqst;
                case 0x1c: /* DSI_VCx_IRQENABLE */
                    TRACEDSI("DSI_VC%d_IRQENABLE = 0x%08x", x, s->dsi.vc[x].irqen);
                    return s->dsi.vc[x].irqen;
                default:
                    OMAP_BAD_REG(addr);
            }
            break;
        
        case 0x200: /* DSI_PHY_CFG0 */
            TRACEDSI("DSI_PHY_CFG0 = 0x%08x", s->dsi.phy_cfg0);
            return s->dsi.phy_cfg0;
        case 0x204: /* DSI_PHY_CFG1 */
            TRACEDSI("DSI_PHY_CFG1 = 0x%08x", s->dsi.phy_cfg1);
            return s->dsi.phy_cfg1;
        case 0x208: /* DSI_PHY_CFG2 */
            TRACEDSI("DSI_PHY_CFG2 = 0x%08x", s->dsi.phy_cfg2);
            return s->dsi.phy_cfg2;
        case 0x214: /* DSI_PHY_CFG5 */
            TRACEDSI("DSI_PHY_CFG5 = 0xfc000000");
            return 0xfc000000; /* all resets done */
            
        case 0x300: /* DSI_PLL_CONTROL */
            TRACEDSI("DSI_PLL_CONTROL = 0x%08x", s->dsi.pll_control);
            return s->dsi.pll_control;
        case 0x304: /* DSI_PLL_STATUS */
            x = 1; /* DSI_PLLCTRL_RESET_DONE */
            if ((s->dsi.clk_ctrl >> 28) & 3) { /* DSI PLL control powered? */
                if (((s->dsi.pll_config1 >> 1) & 0x7f) &&  /* DSI_PLL_REGN */
                    ((s->dsi.pll_config1 >> 8) & 0x7ff)) { /* DSI_PLL_REGM */
                    x |= 2; /* DSI_PLL_LOCK */
                }
            }
            if ((s->dsi.pll_config2 >> 20) & 1) /* DSI_HSDIVBYPASS */
                x |= (1 << 9);                  /* DSI_BYPASSACKZ */
            if (!((s->dsi.pll_config2 >> 13) & 1)) /* DSI_PLL_REFEN */
                x |= (1 << 3);                     /* DSI_PLL_LOSSREF */
            TRACEDSI("DSI_PLL_STATUS = 0x%08x", x);
            return x;
        case 0x308: /* DSI_PLL_GO */
            TRACEDSI("DSI_PLL_GO = 0x%08x", s->dsi.pll_go);
            return s->dsi.pll_go;
        case 0x30c: /* DSI_PLL_CONFIGURATION1 */
            TRACEDSI("DSI_PLL_CONFIGURATION1 = 0x%08x", s->dsi.pll_config1);
            return s->dsi.pll_config1;
        case 0x310: /* DSI_PLL_CONFIGURATION2 */
            TRACEDSI("DSI_PLL_CONFIGURATION2 = 0x%08x", s->dsi.pll_config2);
            return s->dsi.pll_config2;
            
        default:
            break;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_dsi_write(void *opaque, target_phys_addr_t addr,
                           uint32_t value)
{
    struct omap_dss_s *s = (struct omap_dss_s *)opaque;
    uint32_t x;
    
    switch (addr) {
        case 0x000: /* DSI_REVISION */
        case 0x014: /* DSI_SYSSTATUS */
        case 0x07c: /* DSI_RX_FIFO_VC_FULLNESS */
        case 0x084: /* DSI_RX_FIFO_VC_EMPTINESS */
        case 0x214: /* DSI_PHY_CFG5 */
        case 0x304: /* DSI_PLL_STATUS */
            /* read-only, ignore */
            break;
        case 0x010: /* DSI_SYSCONFIG */
            TRACEDSI("DSI_SYSCONFIG = 0x%08x", value);
            if (value & 2) /* SOFT_RESET */
                omap_dsi_reset(s);
            else
                s->dsi.sysconfig = value;
            break;
        case 0x018: /* DSI_IRQSTATUS */
            TRACEDSI("DSI_IRQSTATUS = 0x%08x", value);
            s->dsi.irqst &= ~(value & 0x1fc3b0);
            break;
        case 0x01c: /* DSI_IRQENABLE */
            TRACEDSI("DSI_IRQENABLE = 0x%08x", value);
            s->dsi.irqen = value & 0x1fc3b0;
            break;
        case 0x040: /* DSI_CTRL */
            TRACEDSI("DSI_CTRL = 0x%08x", value);
            s->dsi.ctrl = value & 0x7ffffff;
            break;
        case 0x048: /* DSI_COMPLEXIO_CFG_1 */
            TRACEDSI("DSI_COMPLEXIO_CFG1 = 0x%08x", value);
            value |= 1 << 29; /* RESET_DONE */
            value |= 1 << 21; /* LDO_POWER_GOOD_STATE */
            /* copy PWR_CMD directly to PWR_STATUS */
            value &= ~(3 << 25);
            value |= (value >> 2) & (3 << 25);
            s->dsi.complexio_cfg1 = value;
            break;
        case 0x04c: /* DSI_COMPLEXIO_IRQSTATUS */
            TRACEDSI("DSI_COMPLEXIO_IRQSTATUS = 0x%08x", value);
            s->dsi.complexio_irqst &= ~(value & 0xc3f39ce7);
            if (s->dsi.complexio_irqst)
                s->dsi.irqst |= (1 << 10);  /* COMPLEXIO_ERR_IRQ */
            else
                s->dsi.irqst &= ~(1 << 10); /* COMPLEXIO_ERR_IRQ */
            break;
        case 0x050: /* DSI_COMPLEXIO_IRQENABLE */
            TRACEDSI("DSI_COMPLEXIO_IRQENABLE = 0x%08x", value);
            s->dsi.complexio_irqen = value & 0xc3f39ce7;
            break;
        case 0x054: /* DSI_CLK_CTRL */
            TRACEDSI("DSI_CLK_CTRL = 0x%08x", value);
            value &= 0xc03fffff;
            /* copy PLL_PWR_CMD directly to PLL_PWR_STATUS */
            value |= (value >> 2) & (3 << 28);
            s->dsi.clk_ctrl = value;
            break;
        case 0x058: /* DSI_TIMING1 */
            TRACEDSI("DSI_TIMING1 = 0x%08x", value);
            value &= ~(1 << 15); /* deassert ForceTxStopMode signal */
            s->dsi.timing1 = value;
            break;
        case 0x05c: /* DSI_TIMING2 */
            TRACEDSI("DSI_TIMING2 = 0x%08x", value);
            s->dsi.timing2 = value;
            break;
        case 0x060: /* DSI_VM_TIMING1 */
            TRACEDSI("DSI_VM_TIMING1 = 0x%08x", value);
            s->dsi.vm_timing1 = value;
            break;
        case 0x064: /* DSI_VM_TIMING2 */
            TRACEDSI("DSI_VM_TIMING2 = 0x%08x", value);
            s->dsi.vm_timing2 = value & 0x0fffffff;
            break;
        case 0x068: /* DSI_VM_TIMING3 */
            TRACEDSI("DSI_VM_TIMING3 = 0x%08x", value);
            s->dsi.vm_timing3 = value;
            break;
        case 0x06c: /* DSI_CLK_TIMING */
            TRACEDSI("DSI_CLK_TIMING = 0x%08x", value);
            s->dsi.clk_timing = value & 0xffff;
            break;
        case 0x070: /* DSI_TX_FIFO_VC_SIZE */
            TRACEDSI("DSI_TX_FIFO_VC_SIZE = 0x%08x", value);
            s->dsi.tx_fifo_vc_size = value & 0xf7f7f7f7;
            break;
        case 0x074: /* DSI_RX_FIFO_VC_SIZE */
            TRACEDSI("DSI_RX_FIFO_VC_SIZE = 0x%08x", value);
            s->dsi.rx_fifo_vc_size = value & 0xf7f7f7f7;
            break;
        case 0x078: /* DSI_COMPLEXIO_CFG_2 */
        case 0x080: /* DSI_VM_TIMING4 */
        case 0x088: /* DSI_VM_TIMING5 */
        case 0x08c: /* DSI_VM_TIMING6 */
        case 0x090: /* DSI_VM_TIMING7 */
        case 0x094: /* DSI_STOPCLK_TIMING */
            OMAP_BAD_REGV(addr, value);
            break;
        case 0x100 ... 0x17c: /* DSI_VCx_xxx */
            x = (addr >> 6) & 3;
            switch (addr & 0x1f) {
                case 0x00: /* DSI_VCx_CTRL */
                    TRACEDSI("DSI_VC%d_CTRL = 0x%08x", x, value);
                    if (value & 1) { /* VC_EN */
                        s->dsi.vc[x].ctrl &= ~0x40; /* BTA_EN */
                        s->dsi.vc[x].ctrl |= 0x1;   /* VC_EN */
                    } else {
                        s->dsi.vc[x].ctrl = (s->dsi.vc[x].ctrl & 0x11c020) |
                                            (value & 0x3fee039f);
                    }
//                    if (value & 0x40) { /* BTA_EN */
//                        s->dsi.irqst |= 1 << x;     /* VIRTUAL_CHANNELx_IRQ */
//                        s->dsi.vc[x].irqst |= 0x20; /* BTA_IRQ */
//                        omap_dss_interrupt_update(s);
//                    }
                    break;
                case 0x04: /* DSI_VCx_TE */
                    TRACEDSI("DSI_VC%d_TE = 0x%08x", x, value);
                    value &= 0xc0ffffff;
                    if (s->dsi.vc[x].ctrl & 1) { /* VC_EN */
                        value &= ~(1 << 30);     /* TE_EN */
                        value |= s->dsi.vc[x].te & (1 << 30);
                    }
                    s->dsi.vc[x].te = value;
                    break;
                case 0x08: /* DSI_VCx_LONG_PACKET_HEADER */
                    TRACEDSI("DSI_VC%d_LONG_PACKET_HEADER = 0x%08x", x, value);
                    s->dsi.vc[x].lp_header = value;
                    break;
                case 0x0c: /* DSI_VCx_LONG_PACKET_PAYLOAD */
                    TRACEDSI("DSI_VC%d_LONG_PACKET_PAYLOAD = 0x%08x", x, value);
                    s->dsi.vc[x].lp_payload = value;
                    break;
                case 0x10: /* DSI_VCx_SHORT_PACKET_HEADER */
                    TRACEDSI("DSI_VC%d_SHORT_PACKET_HEADER = 0x%08x", x, value);
                    s->dsi.vc[x].sp_header = value;
                    break;
                case 0x18: /* DSI_VCx_IRQSTATUS */
                    TRACEDSI("DSI_VC%d_IRQSTATUS = 0x%08x", x, value);
                    s->dsi.vc[x].irqst &= ~(value & 0x1ff);
                    if (s->dsi.vc[x].irqst)
                        s->dsi.irqst |= 1 << x;    /* VIRTUAL_CHANNELx_IRQ */
                    else
                        s->dsi.irqst &= ~(1 << x); /* VIRTUAL_CHANNELx_IRQ */
                    break;
                case 0x1c: /* DSI_VCx_IRQENABLE */
                    TRACEDSI("DSI_VC%d_IRQENABLE = 0x%08x", x, value);
                    s->dsi.vc[x].irqen = value & 0x1ff;
                    break;
                default:
                    OMAP_BAD_REG(addr);
            }
            break;
            
        case 0x200: /* DSI_PHY_CFG0 */
            TRACEDSI("DSI_PHY_CFG0 = 0x%08x", value);
            s->dsi.phy_cfg0 = value;
            break;
        case 0x204: /* DSI_PHY_CFG1 */
            TRACEDSI("DSI_PHY_CFG1 = 0x%08x", value);
            s->dsi.phy_cfg1 = value;
            break;
        case 0x208: /* DSI_PHY_CFG2 */
            TRACEDSI("DSI_PHY_CFG2 = 0x%08x", value);
            s->dsi.phy_cfg2 = value;
            break;
            
        case 0x300: /* DSI_PLL_CONTROL */
            TRACEDSI("DSI_PLL_CONTROL = 0x%08x", value);
            s->dsi.pll_control = value & 0x1f;
            break;
        case 0x308: /* DSI_PLL_GO */
            TRACEDSI("DSI_PLL_GO = 0x%08x", value);
            /* TODO: check if we need to update something here */
            value &= ~1; /* mark it done */
            s->dsi.pll_go = value & 1;
            break;
        case 0x30c: /* DSI_PLL_CONFIGURATION1 */
            TRACEDSI("DSI_PLL_CONFIGURATION1 = 0x%08x", value);
            s->dsi.pll_config1 = value & 0x7ffffff;
            break;
        case 0x310: /* DSI_PLL_CONFIGURATION2 */
            TRACEDSI("DSI_PLL_CONFIGURATION2 = 0x%08x", value);
            s->dsi.pll_config2 = value & 0x1fffff;
            break;
            
        default:
            OMAP_BAD_REGV(addr, value);
            break;
    }
}

static CPUReadMemoryFunc *omap_dsi_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_dsi_read,
};

static CPUWriteMemoryFunc *omap_dsi_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_dsi_write,
};

struct omap_dss_s *omap_dss_init(struct omap_mpu_state_s *mpu,
                                 struct omap_target_agent_s *ta,
                                 qemu_irq irq, qemu_irq drq,
                                 omap_clk fck1, omap_clk fck2, omap_clk ck54m,
                                 omap_clk ick1, omap_clk ick2)
{
    int iomemtype[5];
    int region_base = 0;
    struct omap_dss_s *s = (struct omap_dss_s *)
            qemu_mallocz(sizeof(struct omap_dss_s));

    s->irq = irq;
    s->drq = drq;

    iomemtype[0] = l4_register_io_memory(0, omap_diss1_readfn,
                                         omap_diss1_writefn, s);
    iomemtype[1] = l4_register_io_memory(0, omap_disc1_readfn,
                                         omap_disc1_writefn, s);
    iomemtype[2] = l4_register_io_memory(0, omap_rfbi1_readfn,
                                         omap_rfbi1_writefn, s);
    iomemtype[3] = l4_register_io_memory(0, omap_venc1_readfn,
                                         omap_venc1_writefn, s);

    if (cpu_class_omap3(mpu)) {
        s->dispc.rev = 0x30;
        omap_dsi_reset(s);
        
        iomemtype[4] = l4_register_io_memory(0, omap_dsi_readfn,
                                             omap_dsi_writefn, s);
        omap_l4_attach(ta, 0, iomemtype[4]);
        region_base = 1;
    } else {
        s->dispc.rev = 0x20;
        
        iomemtype[4] = cpu_register_io_memory(0, omap_im3_readfn,
                                              omap_im3_writefn, s);
        cpu_register_physical_memory(0x68000800, 0x1000, iomemtype[4]);
    }
    
    omap_l4_attach(ta, region_base+0, iomemtype[0]); /* DISS */
    omap_l4_attach(ta, region_base+1, iomemtype[1]); /* DISC */
    omap_l4_attach(ta, region_base+2, iomemtype[2]); /* RFBI */
    omap_l4_attach(ta, region_base+3, iomemtype[3]); /* VENC */

    omap_dss_reset(s);

#if 0
    s->state = graphic_console_init(omap_update_display,
                                    omap_invalidate_display, omap_screen_dump, s);
#endif

    register_savevm("omap_dss", -1, 0,
                    omap_dss_save_state, omap_dss_load_state, s);
    
    return s;
}

void omap_rfbi_attach(struct omap_dss_s *s, int cs, struct rfbi_chip_s *chip)
{
    if (cs < 0 || cs > 1)
        cpu_abort(cpu_single_env, "%s: wrong CS %i\n", __FUNCTION__, cs);
    s->rfbi.chip[cs] = chip;
}

void omap3_lcd_panel_attach(struct omap_dss_s *dss,
                            int cs,
                            struct omap3_lcd_panel_s *lcd_panel)
{
    if (cs < 0 || cs > 1)
        cpu_abort(cpu_single_env, "%s: wrong CS %i\n", __FUNCTION__, cs);
    dss->omap_lcd_panel[cs] = lcd_panel;
	lcd_panel->dss = dss;
}

/*omap3 lcd panel stuff*/

/* Bytes(!) per pixel */
static const int omap3_lcd_panel_bpp[0x10] = {
    0,  /* 0x0: BITMAP1 (CLUT) */
    0,  /* 0x1: BITMAP2 (CLUT) */
    0,  /* 0x2: BITMAP4 (CLUT) */
    0,  /* 0x3: BITMAP8 (CLUT) */
    2,  /* 0x4: RGB12 (unpacked 16-bit container)*/
    2,  /* 0x5: ARGB16 */
    2,  /* 0x6: RGB16 */
    0,  /* 0x7: reserved */
    4,  /* 0x8: RGB24 (unpacked in 32-bit container) */
    3,  /* 0x9: RGB24 (packed in 24-bit container) */
    2,  /* 0xa: YUV2 422 */
    2,  /* 0xb: UYVY 422 */
    4,  /* 0xc: ARGB32 */
    4,  /* 0xd: RGBA32 */
    4,  /* 0xe: RGBx32 (24-bit RGB aligned on MSB of the 32-bit container) */
    0,  /* 0xf: reserved */
};

static inline void omap3_lcd_panel_invalidate_display(void *opaque) 
{
    struct omap3_lcd_panel_s *s = (struct omap3_lcd_panel_s *)opaque;
    s->invalidate = 1;
}

static void omap3_lcd_panel_update_display(void *opaque)
{
    struct omap3_lcd_panel_s *s = (struct omap3_lcd_panel_s *)opaque;
    struct omap_dss_s *dss = s->dss;
    const uint32_t lcd_width = dss->lcd.nx;
    const uint32_t lcd_height = dss->lcd.ny;
    uint32_t graphic_width, graphic_height;
    uint32_t start_x, start_y;
    const uint32_t lcd_Bpp = omap3_lcd_panel_bpp[dss->dispc.l[0].gfx_format];
    uint32_t dss_Bpp;
    uint32_t linesize, y;
    uint32_t copy_width, copy_height;
    uint8_t *src, *dest;

    if (!dss->lcd.enable
        || dss->dispc.l[0].gfx_channel /* 24bit digital out */
        || ((dss->dispc.control & (1 << 11))) /* RFBI */
        || !lcd_Bpp)
        return;
    
    /* check for setup changes since last visit only if flagged */
    if (dss->dispc.invalidate) {
        dss->dispc.invalidate = 0;
        if (!(dss->dispc.l[0].rotation_flag)) {	  /* rotation*/
            s->line_fn = s->line_fn_tab[0][dss->dispc.l[0].gfx_format];
        } else {
            fprintf(stderr, "%s: rotation is not supported \n", __FUNCTION__);
            exit(1);
        }
        if (!s->line_fn) {
            fprintf(stderr,
                    "%s: line_fn is NULL - unsupported gfx_format (%d)\n",
                    __FUNCTION__, dss->dispc.l[0].gfx_format);
            exit(1);
        }
        if (lcd_width != ds_get_width(s->state) 
            || lcd_height != ds_get_height(s->state)) {
            qemu_console_resize(s->state, lcd_width, lcd_height);
            s->invalidate = 1;
        }
    }
    
    /* Resolution */
    graphic_width = dss->dispc.l[0].nx;
    graphic_height = dss->dispc.l[0].ny;
    start_x = dss->dispc.l[0].posx;
    start_y = dss->dispc.l[0].posy;

    /*use the rfbi function*/
    src = (uint8_t *)omap_rfbi_get_buffer(dss);
    dest = ds_get_data(s->state);
    linesize = ds_get_linesize(s->state);

    dss_Bpp = linesize / ds_get_width(s->state);

    dest += linesize * start_y;
    dest += start_x * dss_Bpp;

    if ((start_x + graphic_width) > lcd_width)
        copy_width = lcd_width - start_x;
    else
    	copy_width = graphic_width;
    copy_height = lcd_height>graphic_height ? graphic_height:lcd_height;

    for (y = start_y; y < copy_height; y++) {
        s->line_fn(dest, src, copy_width * lcd_Bpp);
        src += graphic_width * lcd_Bpp;
        dest += linesize;
    }

    dpy_update(s->state, start_x, start_y, graphic_width, graphic_height);
    s->invalidate = 0;
    
    dss->dispc.irqst |= 1; /* FRAMEDONE */
    omap_dss_interrupt_update(dss);
}

/*omap lcd stuff*/
#define DEPTH 8
#include "omap3_lcd_panel_template.h"
#define DEPTH 15
#include "omap3_lcd_panel_template.h"
#define DEPTH 16
#include "omap3_lcd_panel_template.h"
#define DEPTH 24
#include "omap3_lcd_panel_template.h"
#define DEPTH 32
#include "omap3_lcd_panel_template.h"

void *omap3_lcd_panel_init()
{
    struct omap3_lcd_panel_s *s = (struct omap3_lcd_panel_s *) qemu_mallocz(sizeof(*s));

    s->state = graphic_console_init(omap3_lcd_panel_update_display,
                                    omap3_lcd_panel_invalidate_display,
                                    NULL, NULL, s);

    switch (ds_get_bits_per_pixel(s->state)) {
    case 0:
        s->line_fn_tab[0] = s->line_fn_tab[1] =
            qemu_mallocz(sizeof(omap3_lcd_panel_fn_t) * 0x10);
        break;
    case 8:
        s->line_fn_tab[0] =  omap3_lcd_panel_draw_fn_8;
        s->line_fn_tab[1] =  omap3_lcd_panel_draw_fn_r_8;
        break;
    case 15:
        s->line_fn_tab[0] =  omap3_lcd_panel_draw_fn_15;
        s->line_fn_tab[1] =  omap3_lcd_panel_draw_fn_r_15;
        break;
    case 16:
        s->line_fn_tab[0] =  omap3_lcd_panel_draw_fn_16;
        s->line_fn_tab[1] =  omap3_lcd_panel_draw_fn_r_16;
        break;
    case 24:
        s->line_fn_tab[0] =  omap3_lcd_panel_draw_fn_24;
        s->line_fn_tab[1] =  omap3_lcd_panel_draw_fn_r_24;
        break;
    case 32:
        s->line_fn_tab[0] =  omap3_lcd_panel_draw_fn_32;
        s->line_fn_tab[1] =  omap3_lcd_panel_draw_fn_r_32;
        break;
    default:
        fprintf(stderr, "%s: Bad color depth\n", __FUNCTION__);
        exit(1);
    }

    return s;
}
