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


#define SKIP_PIXEL(to)		to += deststep
#if DEPTH == 8
# define PIXEL_TYPE		uint8_t
# define COPY_PIXEL(to, from)	*to = from; SKIP_PIXEL(to)
# define COPY_PIXEL1(to, from)	*to ++ = from
#elif DEPTH == 15 || DEPTH == 16
# define PIXEL_TYPE		uint16_t
# define COPY_PIXEL(to, from)	*to = from; SKIP_PIXEL(to)
# define COPY_PIXEL1(to, from)	*to ++ = from
#elif DEPTH == 24
# define PIXEL_TYPE		uint8_t
# define COPY_PIXEL(to, from)	\
    to[0] = from; to[1] = (from) >> 8; to[2] = (from) >> 16; SKIP_PIXEL(to)
# define COPY_PIXEL1(to, from)	\
    *to ++ = from; *to ++ = (from) >> 8; *to ++ = (from) >> 16
#elif DEPTH == 32
# define PIXEL_TYPE		uint32_t
# define COPY_PIXEL(to, from)	*to = from; SKIP_PIXEL(to)
# define COPY_PIXEL1(to, from)	*to ++ = from
#else
# error unknown bit depth
#endif

#ifdef WORDS_BIGENDIAN
# define SWAP_WORDS	1
#endif


static void glue(omap3_lcd_panel_draw_line16_, DEPTH)(PIXEL_TYPE *dest,
                const uint16_t *src, unsigned int width)
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


/*
LCD: 0x4: RGB 12      
        0x5: ARGB16
        0x6: RGB 16
        0x8: RGB 24 (un-packed in 32-bit container)
        0x9: RGB 24 (packed in 24-bit container)
        0xc: ARGB32
        0xd: RGBA32
        0xe: RGBx 32 (24-bit RGB aligned on MSB of the 32-bit container)

SDL:  8/16/24/32

*/

/* No rotation */
static omap3_lcd_panel_fn_t glue(omap3_lcd_panel_draw_fn_, DEPTH)[0x10] = {
    NULL,   /*0x0*/
    NULL,   /*0x1*/
    NULL,   /*0x2*/
    NULL,   /*0x3*/
    NULL,  /*0x4:RGB 12 */
    NULL,  /*0x5: ARGB16 */
    (omap3_lcd_panel_fn_t)glue(omap3_lcd_panel_draw_line16_, DEPTH),  /*0x6: RGB 16 */
    NULL,  /*0x7*/
    NULL,  /*0x8: RGB 24 (un-packed in 32-bit container) */
    NULL,  /*0x9: RGB 24 (packed in 24-bit container) */
    NULL,  /*0xa */
    NULL,  /*0xb */
    NULL,  /*0xc: ARGB32 */
    NULL,  /*0xd: RGBA32 */
    NULL,  /*0xe: RGBx 32 (24-bit RGB aligned on MSB of the 32-bit container) */
    NULL,  /*0xf */
};

/* 90deg, 180deg and 270deg rotation */
static omap3_lcd_panel_fn_t glue(omap3_lcd_panel_draw_fn_r_, DEPTH)[0x10] = {
    /* TODO */
    [0 ... 0xf] = NULL,
};

#undef DEPTH
#undef SKIP_PIXEL
#undef COPY_PIXEL
#undef COPY_PIXEL1
#undef PIXEL_TYPE

#undef SWAP_WORDS

 
