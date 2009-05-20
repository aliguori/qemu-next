/*
 * QEMU Epson S1D13744/S1D13745 templates
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * QEMU OMAP3 LCD Panel Emulation templates
 *
 * Copyright (c) 2008 yajin  <yajin@vm-kernel.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


#if DEPTH == 8
# define PIXEL_TYPE		uint8_t
# define COPY_PIXEL1(to, from)	*to ++ = from
#elif DEPTH == 15 || DEPTH == 16
# define PIXEL_TYPE		uint16_t
# define COPY_PIXEL1(to, from)	*to ++ = from
#elif DEPTH == 24
# define PIXEL_TYPE		uint8_t
# define COPY_PIXEL1(to, from)	\
    *to ++ = from; *to ++ = (from) >> 8; *to ++ = (from) >> 16
#elif DEPTH == 32
# define PIXEL_TYPE		uint32_t
# define COPY_PIXEL1(to, from)	*to ++ = from
#else
# error unknown bit depth
#endif

#ifdef WORDS_BIGENDIAN
# define SWAP_WORDS	1
#endif


static void glue(omap3_lcd_panel_draw_line16_, DEPTH)(PIXEL_TYPE *dest,
                                                      const uint16_t *src,
                                                      unsigned int width)
{
#if !defined(SWAP_WORDS) && DEPTH == 16
    memcpy(dest, src, width);
#else
    uint16_t data;
    unsigned int r, g, b;
    const uint16_t *end = (const void *) src + width;
    while (src < end) {
        data = lduw_raw(src ++);
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x3f) << 2;
        data >>= 6;
        r = (data & 0x1f) << 3;
        data >>= 5;
        COPY_PIXEL1(dest, glue(rgb_to_pixel, DEPTH)(r, g, b));
    }
#endif
}

static void glue(omap3_lcd_panel_draw_line24a_, DEPTH)(PIXEL_TYPE *dest,
                                                       const uint8_t *src,
                                                       unsigned int width)
{
#if !defined(SWAP_WORDS) && DEPTH == 32
    memcpy(dest, src, width);
#else
    unsigned int r, g, b;
    const uint8_t *end = (const void *) src + width;
    while (src < end) {
        b = *(src++);
        g = *(src++);
        r = *(src++);
        src++;
        COPY_PIXEL1(dest, glue(rgb_to_pixel, DEPTH)(r, g, b));
    }
#endif
}

static void glue(omap3_lcd_panel_draw_line24b_, DEPTH)(PIXEL_TYPE *dest,
                                                       const uint8_t *src,
                                                       unsigned int width)
{
#if DEPTH == 24
    memcpy(dest, src, width);
#else
    unsigned int r, g, b;
    const uint8_t *end = (const void *) src + width;
    while (src < end) {
        b = *(src++);
        g = *(src++);
        r = *(src++);
        COPY_PIXEL1(dest, glue(rgb_to_pixel, DEPTH)(r, g, b));
    }
#endif
}

/* No rotation */
static omap3_lcd_panel_fn_t glue(omap3_lcd_panel_draw_fn_, DEPTH)[0x10] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    (omap3_lcd_panel_fn_t)glue(omap3_lcd_panel_draw_line16_, DEPTH),
    NULL,
    (omap3_lcd_panel_fn_t)glue(omap3_lcd_panel_draw_line24a_, DEPTH),
    (omap3_lcd_panel_fn_t)glue(omap3_lcd_panel_draw_line24b_, DEPTH),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

/* 90deg, 180deg and 270deg rotation */
//static omap3_lcd_panel_fn_t glue(omap3_lcd_panel_draw_fn_r_, DEPTH)[0x10] = {
//    /* TODO */
//    [0 ... 0xf] = NULL,
//};

#undef DEPTH
#undef SKIP_PIXEL
#undef COPY_PIXEL
#undef COPY_PIXEL1
#undef PIXEL_TYPE

#undef SWAP_WORDS

 
