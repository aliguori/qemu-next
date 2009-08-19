/*
 * OMAP3 Generic Parallel Interface LCD Panel (non-RFBI) support
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
#include "hw.h"
#include "console.h"
#include "omap.h"
#include "qemu-common.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "devices.h"
#include "pixel_ops.h"
#include "omap_dss.h"

typedef void (*omap3_lcd_panel_fn_t)(uint8_t *, const uint8_t *, unsigned int);

struct omap3_lcd_panel_s {
    struct omap_dss_s *dss;
    DisplayState *state;
    uint32_t invalidate;
    struct omap_dss_panel_s panel;
    
    uint32_t control;
    uint32_t width;
    uint32_t height;
    uint32_t gfx_attr;
    uint32_t gfx_width;
    uint32_t gfx_height;
    uint32_t gfx_posx;
    uint32_t gfx_posy;
    target_phys_addr_t gfx_addr;
};

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

static void omap3_lcd_panel_control_update(void *opaque,
                                           const struct omap_dss_dispc_s *dispc)
{
    struct omap3_lcd_panel_s *s = (struct omap3_lcd_panel_s *)opaque;
    
    s->invalidate = 1;
    s->control = dispc->control;
    s->width = (dispc->size_lcd & 0x7ff) + 1;
    s->height = ((dispc->size_lcd >> 16) & 0x7ff) + 1;
    s->gfx_attr = dispc->l[0].attr;
    s->gfx_width = dispc->l[0].nx;
    s->gfx_height = dispc->l[0].ny;
    s->gfx_posx = dispc->l[0].posx;
    s->gfx_posy = dispc->l[0].posy;
    s->gfx_addr = dispc->l[0].addr[0];
}

static void omap3_lcd_panel_invalidate_display(void *opaque) 
{
    struct omap3_lcd_panel_s *s = (struct omap3_lcd_panel_s *)opaque;
    s->invalidate = 1;
}

void omap3_lcd_panel_layer_update(DisplayState *ds,
                                  uint32_t lcd_width, uint32_t lcd_height,
                                  uint32_t posx, uint32_t posy,
                                  uint32_t width, uint32_t height,
                                  uint32_t attrib,
                                  target_phys_addr_t addr)
{
    if (!(attrib & 1)) { /* layer disabled? */
        return;
    }
    uint32_t format = (attrib >> 1) & 0xf;
    omap3_lcd_panel_fn_t line_fn = 0;
    switch (ds_get_bits_per_pixel(ds)) {
        case 8:  line_fn = omap3_lcd_panel_draw_fn_8[format]; break;
        case 15: line_fn = omap3_lcd_panel_draw_fn_15[format]; break;
        case 16: line_fn = omap3_lcd_panel_draw_fn_16[format]; break;
        case 24: line_fn = omap3_lcd_panel_draw_fn_24[format]; break;
        case 32: line_fn = omap3_lcd_panel_draw_fn_32[format]; break;
        default: line_fn = 0; break;
    }
    if (!line_fn) {
        return;
    }
    
    const uint32_t lcd_Bpp = omap_lcd_Bpp[format];

    uint32_t graphic_width = width;
    uint32_t graphic_height = height;
    uint32_t start_x = posx;
    uint32_t start_y = posy;
    
    uint8_t *dest = ds_get_data(ds);
    uint32_t linesize = ds_get_linesize(ds);
    
    uint32_t host_Bpp = linesize / ds_get_width(ds);
    
    dest += linesize * start_y;
    dest += start_x * host_Bpp;
    
    uint32_t copy_width = (start_x + graphic_width) > lcd_width
                          ? (lcd_width - start_x) : graphic_width;
    uint32_t copy_height = lcd_height > graphic_height
                          ? graphic_height : lcd_height;
    
    target_phys_addr_t size = copy_height * copy_width * lcd_Bpp;
    uint8_t *src = cpu_physical_memory_map(addr, &size, 0);
    if (src) {
        if (size == copy_height * copy_width * lcd_Bpp) {
            uint32_t y;
            for (y = start_y; y < copy_height; y++) {
                line_fn(dest, src, copy_width * lcd_Bpp);
                src += graphic_width * lcd_Bpp;
                dest += linesize;
            }
        } else {
            hw_error("%s: rendering uncontiguous framebuffer is not supported",
                    __FUNCTION__);
        }
        cpu_physical_memory_unmap(src, size, 0, size);
    }
}

static void omap3_lcd_panel_update_display(void *opaque)
{
    struct omap3_lcd_panel_s *s = (struct omap3_lcd_panel_s *)opaque;
    
    if (!(s->control & 1)            /* LCDENABLE */
        || (s->gfx_attr & 0x100)     /* GFXCHANNELOUT */
        || (s->control & (1 << 11))) /* STALLMODE */
        return;
    
    if (s->invalidate) {
        s->invalidate = 0;
        if (s->width != ds_get_width(s->state) 
            || s->height != ds_get_height(s->state)) {
            qemu_console_resize(s->state, s->width, s->height);
        }
        if ((s->gfx_attr >> 12) & 0x3) { /* GFXROTATION */
            hw_error("%s: GFX rotation is not supported", __FUNCTION__);
        }
    }
    
    /* TODO: draw background color */
    
    omap3_lcd_panel_layer_update(s->state,
                                 s->width, s->height,
                                 s->gfx_posx, s->gfx_posy,
                                 s->gfx_width, s->gfx_height,
                                 s->gfx_attr,
                                 s->gfx_addr);
    
    /* TODO: draw VID1 & VID2 layers */
    
    dpy_update(s->state, 0, 0, s->width, s->height);

    omap_dss_lcd_framedone(s->dss);
}

static void omap3_lcd_panel_save_state(QEMUFile *f, void *opaque)
{
    struct omap3_lcd_panel_s *s = (struct omap3_lcd_panel_s *)opaque;
    
    qemu_put_be32(f, s->control);
    qemu_put_be32(f, s->width);
    qemu_put_be32(f, s->height);
    qemu_put_be32(f, s->gfx_attr);
    qemu_put_be32(f, s->gfx_width);
    qemu_put_be32(f, s->gfx_height);
    qemu_put_be32(f, s->gfx_posx);
    qemu_put_be32(f, s->gfx_posy);
    qemu_put_be32(f, s->gfx_addr);
}

static int omap3_lcd_panel_load_state(QEMUFile *f, void *opaque, int version_id)
{
    struct omap3_lcd_panel_s *s = (struct omap3_lcd_panel_s *)opaque;
    
    if (version_id)
        return -EINVAL;
    
    s->control = qemu_get_be32(f);
    s->width = qemu_get_be32(f);
    s->height = qemu_get_be32(f);
    s->gfx_attr = qemu_get_be32(f);
    s->gfx_width = qemu_get_be32(f);
    s->gfx_height = qemu_get_be32(f);
    s->gfx_posx = qemu_get_be32(f);
    s->gfx_posy = qemu_get_be32(f);
    s->gfx_addr = qemu_get_be32(f);
    
    s->invalidate = 1;
    
    return 0;
}

struct omap3_lcd_panel_s *omap3_lcd_panel_init(struct omap_dss_s *dss)
{
    struct omap3_lcd_panel_s *s = (struct omap3_lcd_panel_s *) qemu_mallocz(sizeof(*s));

    s->dss = dss;
    s->invalidate = 1;
    s->panel.opaque = s;
    s->panel.controlupdate = omap3_lcd_panel_control_update;
    s->state = graphic_console_init(omap3_lcd_panel_update_display,
                                    omap3_lcd_panel_invalidate_display,
                                    NULL, NULL, s);
    register_savevm("omap3_lcd_panel", -1, 0,
                    omap3_lcd_panel_save_state, omap3_lcd_panel_load_state, s);
    return s;
}

const struct omap_dss_panel_s *omap3_lcd_panel_get(struct omap3_lcd_panel_s *s)
{
    return &s->panel;
}
