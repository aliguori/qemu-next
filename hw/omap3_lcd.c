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
#include "framebuffer.h"

//#define PROFILE_FRAMEUPDATE

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
    target_phys_addr_t gfx_table;
    uint32_t gfx_palette_size;
    uint32_t *gfx_palette;
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
    struct omap3_lcd_panel_s *s = opaque;
    int n;
    
    s->invalidate = 1;
    s->control = dispc->control;
    s->width = (dispc->size_lcd & 0x7ff) + 1;
    s->height = ((dispc->size_lcd >> 16) & 0x7ff) + 1;
    s->gfx_attr = dispc->plane[0].attr;
    s->gfx_width = dispc->plane[0].nx;
    s->gfx_height = dispc->plane[0].ny;
    s->gfx_posx = dispc->plane[0].posx;
    s->gfx_posy = dispc->plane[0].posy;
    s->gfx_addr = dispc->plane[0].addr[0];
    s->gfx_table = dispc->plane[0].addr[2];
    
    switch ((s->gfx_attr >> 1) & 0x0f) {
        case 0:
            n = 2;
            break;
        case 1:
            n = 4;
            break;
        case 2:
            n = 16;
            break;
        case 3:
            n = 256;
            break;
        default:
            n = 0;
            break;
    }
    if (n != s->gfx_palette_size) {
        if (s->gfx_palette) {
            qemu_free(s->gfx_palette);
            s->gfx_palette = NULL;
        }
        if (n) {
            s->gfx_palette = qemu_mallocz(n * sizeof(uint32_t));
        }
        s->gfx_palette_size = n;
    }
}

static void omap3_lcd_panel_invalidate_display(void *opaque) 
{
    struct omap3_lcd_panel_s *s = opaque;
    s->invalidate = 1;
}

void omap3_lcd_panel_layer_update(DisplayState *ds,
                                  uint32_t lcd_width, uint32_t lcd_height,
                                  uint32_t posx,
                                  int *posy, int *endy,
                                  uint32_t width, uint32_t height,
                                  uint32_t attrib,
                                  target_phys_addr_t addr,
                                  uint32_t *palette,
                                  int full_update)
{
    if (!(attrib & 1)) { /* layer disabled? */
        return;
    }
    uint32_t format = (attrib >> 1) & 0xf;
    if (format == 0x09 && (attrib & 0x300)) {
        hw_error("graphics rotation (%d) not supported",
                 (attrib >> 12) & 3);
        return;
    }
    drawfn line_fn = 0;
    switch (ds_get_bits_per_pixel(ds)) {
        case 8:  line_fn = omap3_lcd_panel_draw_fn_8[format]; break;
        case 15: line_fn = omap3_lcd_panel_draw_fn_15[format]; break;
        case 16: line_fn = omap3_lcd_panel_draw_fn_16[format]; break;
        case 24: line_fn = omap3_lcd_panel_draw_fn_24[format]; break;
        case 32: line_fn = omap3_lcd_panel_draw_fn_32[format]; break;
        default:
            hw_error("unsupported host display color depth: %d",
                     ds_get_bits_per_pixel(ds));
            return;
    }
    if (!line_fn) {
        hw_error("unsupported omap3 dss color format: %d", format);
        return;
    }

    if (posx) {
        fprintf(stderr, "%s@%d: non-zero layer x-coordinate (%d), "
                "not currently supported -> using zero\n", __FUNCTION__,
                __LINE__, posx);
        posx = 0;
    }
    
#ifdef PROFILE_FRAMEUPDATE
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t prof_time = tv.tv_sec * 1000000LL + tv.tv_usec;
#endif
    
    uint32_t copy_width = (posx + width) > lcd_width
                          ? (lcd_width - posx) : width;
    uint32_t copy_height = ((*posy) + height) > lcd_height
                          ? (lcd_height - (*posy)) : height;
    uint32_t linesize = ds_get_linesize(ds);
    framebuffer_update_display(ds, addr, copy_width, copy_height,
                               (format < 3)
                               ? (width >> (3 - format))
                               : (width * omap_lcd_Bpp[format]),
                               linesize, linesize / ds_get_width(ds),
                               full_update, line_fn, palette,
                               posy, endy);

#ifdef PROFILE_FRAMEUPDATE
    gettimeofday(&tv, NULL);
    prof_time = (tv.tv_sec * 1000000LL + tv.tv_usec) - prof_time;
    printf("%s: framebuffer updated in %lldus\n", __FUNCTION__, prof_time);
#endif
}

static void omap3_lcd_panel_update_palette(uint32_t *palette,
                                           uint32_t palette_size,
                                           target_phys_addr_t table)
{
    palette_size *= sizeof(uint32_t);
    target_phys_addr_t len = palette_size;
    uint32_t *src = cpu_physical_memory_map(table, &len, 0);
    if (!src || len < palette_size) {
        /* cannot read palette data -> clear palette */
        memset(palette, 0, palette_size);
    } else {
        memcpy(palette, src, palette_size);
    }
    if (src) {
        cpu_physical_memory_unmap(src, len, 0, 0);
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
        if (s->width != ds_get_width(s->state) 
            || s->height != ds_get_height(s->state)) {
            qemu_console_resize(s->state, s->width, s->height);
        }
        if ((s->gfx_attr >> 12) & 0x3) { /* GFXROTATION */
            hw_error("%s: GFX rotation is not supported", __FUNCTION__);
        }
    }

    /* TODO: draw background color */

    if (s->gfx_palette) {
        omap3_lcd_panel_update_palette(s->gfx_palette,
                                       s->gfx_palette_size,
                                       s->gfx_table);
    }
    int first_row = s->gfx_posy;
    int last_row = 0;
    omap3_lcd_panel_layer_update(s->state, s->width, s->height,
                                 s->gfx_posx, &first_row, &last_row,
                                 s->gfx_width, s->gfx_height,
                                 s->gfx_attr, s->gfx_addr,
                                 s->gfx_palette, s->invalidate);
    /* TODO: draw VID1 & VID2 layers */
    s->invalidate = 0;
    
    if (first_row >= 0) {
        dpy_update(s->state, 0, first_row, s->width,
                   last_row - first_row + 1);
    }

    omap_dss_lcd_framedone(s->dss);
}

struct omap3_lcd_panel_s *omap3_lcd_panel_init(struct omap_dss_s *dss)
{
    struct omap3_lcd_panel_s *s = qemu_mallocz(sizeof(*s));

    s->dss = dss;
    s->invalidate = 1;
    s->panel.opaque = s;
    s->panel.controlupdate = omap3_lcd_panel_control_update;
    s->state = graphic_console_init(omap3_lcd_panel_update_display,
                                    omap3_lcd_panel_invalidate_display,
                                    NULL, NULL, s);
    return s;
}

const struct omap_dss_panel_s *omap3_lcd_panel_get(struct omap3_lcd_panel_s *s)
{
    return &s->panel;
}
