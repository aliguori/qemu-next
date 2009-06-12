/*
 * Texas Instruments TMS9918 Video Processor emulation rendering templates
 *
 * Copyright (c) 2009 Juha Riihimäki
 * Based on fMSX X11 screen drivers written by Arnold Metselaar
 *
 * This code is licensed under the GPL version 2
 */
#if DEPTH == 8 || DEPTH == 24
#   define PIXEL_TYPE uint8_t
#elif DEPTH == 15 || DEPTH == 16
#   define PIXEL_TYPE uint16_t
#elif DEPTH == 32
#   define PIXEL_TYPE uint32_t
#else
#   error unknown rendering bit depth
#endif

#if DEPTH == 8 || DEPTH == 15 || DEPTH == 16 || DEPTH == 32
#   if SCREEN_ZOOM == 1
#       define ADVANCE_PIXELS(to, n) to += (n)
#       define RETRACE_PIXELS(to, n) to -= (n)
#       define COPY_PIXEL(to, from) *(to++) = from
#   elif SCREEN_ZOOM == 2
#       define ADVANCE_PIXELS(to, n) to += (n) * SCREEN_ZOOM
#       define RETRACE_PIXELS(to, n) to -= (n) * SCREEN_ZOOM
#       define COPY_PIXEL(to, from) { PIXEL_TYPE pix = from; \
            to[width / sizeof(PIXEL_TYPE)] = pix; *(to++) = pix; \
            to[width / sizeof(PIXEL_TYPE)] = pix; *(to++) = pix; }
#   else
#       error unknown rendering zoom factor
#   endif
#elif DEPTH == 24
#   if SCREEN_ZOOM == 1
#       define ADVANCE_PIXELS(to, n) to += (n) * 3
#       define RETRACE_PIXELS(to, n) to -= (n) * 3
#       define COPY_PIXEL(to, from) { PIXEL_TYPE pix = from; \
            *(to++) = pix; *(to++) = pix >> 8; *(to++) = pix >> 16; }
#   elif SCREEN_ZOOM == 2
#       define ADVANCE_PIXELS(to, n) to += (n) * (3 * SCREEN_ZOOM)
#       define RETRACE_PIXELS(to, n) to -= (n) * (3 * SCREEN_ZOOM)
#       define COPY_PIXEL(to, from) { PIXEL_TYPE pix = from; \
            to[width] = pix; *(to++) = pix; \
            to[width] = pix >> 8; *(to++) = pix >> 8; \
            to[width] = pix >> 16; *(to++) = pix >> 16; \
            to[width] = pix; *(to++) = pix; \
            to[width] = pix >> 8; *(to++) = pix >> 8; \
            to[width] = pix >> 16; *(to++) = pix >> 16; }
#   else
#       error unknown rendering zoom factor
#   endif
#else
#   error unknown rendering bit depth
#endif

#define DEPTH_GLUE(x) glue(x, DEPTH)
#define DEPTH_Z_GLUE(x) glue(glue(DEPTH_GLUE(x), _z), SCREEN_ZOOM)
#define FUNC(x) DEPTH_Z_GLUE(x)

#define COLOR(color_index) DEPTH_GLUE(v9918_color_)(color_index)
#if SCREEN_ZOOM == 1
static inline PIXEL_TYPE DEPTH_GLUE(v9918_color_)(int color_index)
{
    color_index *= 3;
    return glue(rgb_to_pixel, DEPTH)(V9918Palette[color_index],
                                     V9918Palette[color_index + 1],
                                     V9918Palette[color_index + 2]);
}
#endif

static inline PIXEL_TYPE *FUNC(v9918_render_color_)(PIXEL_TYPE *p,
                                                    PIXEL_TYPE c,
                                                    unsigned int width)
{
    int i = SCREEN_WIDTH;
    while (i--) {
        COPY_PIXEL(p, c);
    }
    return p;
}

static inline void FUNC(v9918_render_borders_)(PIXEL_TYPE *p,
                                               PIXEL_TYPE bc,
                                               unsigned int width)
{
    RETRACE_PIXELS(p, SCREEN_WIDTH + BORDER_SIZE);
    int i = BORDER_SIZE;
    while (i--) {
        COPY_PIXEL(p, bc);
    }
    ADVANCE_PIXELS(p, SCREEN_WIDTH);
    for (i = BORDER_SIZE; i--;) {
        COPY_PIXEL(p, bc);
    }
}

static inline PIXEL_TYPE *FUNC(v9918_get_line_ptr_)(int scanline,
                                                    PIXEL_TYPE *p,
                                                    PIXEL_TYPE bc,
                                                    unsigned int width)
{
    ADVANCE_PIXELS(p, BORDER_SIZE);
    if (scanline < BORDER_SIZE || scanline >= BORDER_SIZE + SCREEN_HEIGHT) {
        p = DEPTH_Z_GLUE(v9918_render_color_)(p, bc, width);
        DEPTH_Z_GLUE(v9918_render_borders_)(p, bc, width);
        p = NULL;
    }
    return p;
}

static void FUNC(v9918_render0_)(V9918State *s,
                                 int scanline,
                                 PIXEL_TYPE *p,
                                 unsigned int width)
{
    const PIXEL_TYPE bc = COLOR(BG_COLOR);
    p = FUNC(v9918_get_line_ptr_)(scanline, p, bc, width);
    if (p) {
        int x = FG_COLOR;
        if (!x || BLANK_ENABLE) {
            p = FUNC(v9918_render_color_)(p, bc, width);
        } else {
            scanline -= BORDER_SIZE;

            COPY_PIXEL(p, bc); COPY_PIXEL(p, bc);
            COPY_PIXEL(p, bc); COPY_PIXEL(p, bc);
            COPY_PIXEL(p, bc); COPY_PIXEL(p, bc);
            COPY_PIXEL(p, bc); COPY_PIXEL(p, bc);

            const PIXEL_TYPE fc = COLOR(x);
            const uint8_t *src = s->vram + CHRGEN(0, 0x800) + (scanline & 7);
            const int m = ~CHRTAB_MSK(0, 10);
            int t = CHRTAB((scanline >> 3) * 40, 0, 0x400);
            for (x = 40; x; x--) {
                if (t & m) {
                    t = CHRTAB((scanline >> 3) * 40 + (40 - x), 0, 0x400);
                }
                const int k = *(src + ((int)(s->vram[t++]) << 3));
                COPY_PIXEL(p, ((k & 0x80) ? fc : bc));
                COPY_PIXEL(p, ((k & 0x40) ? fc : bc));
                COPY_PIXEL(p, ((k & 0x20) ? fc : bc));
                COPY_PIXEL(p, ((k & 0x10) ? fc : bc));
                COPY_PIXEL(p, ((k & 0x08) ? fc : bc));
                COPY_PIXEL(p, ((k & 0x04) ? fc : bc));
            }
            
            COPY_PIXEL(p, bc); COPY_PIXEL(p, bc);
            COPY_PIXEL(p, bc); COPY_PIXEL(p, bc);
            COPY_PIXEL(p, bc); COPY_PIXEL(p, bc);
            COPY_PIXEL(p, bc); COPY_PIXEL(p, bc);
        }
        FUNC(v9918_render_borders_)(p, bc, width);
    }
}

static void FUNC(v9918_render1_)(V9918State *s,
                                 int scanline,
                                 PIXEL_TYPE *p,
                                 unsigned int width)
{
    PIXEL_TYPE bc = COLOR(BG_COLOR);
    p = FUNC(v9918_get_line_ptr_)(scanline, p, bc, width);
    if (p) {
        if (BLANK_ENABLE) {
            p = FUNC(v9918_render_color_)(p, bc, width);
        } else {
            scanline -= BORDER_SIZE;
            
            const uint8 *src = s->vram + CHRGEN(0, 0x800) + (scanline & 7);
            const uint8 *coltab = s->vram + COLTAB(0, 0x40);
            const uint8 *t = s->vram + CHRTAB((scanline & 0xf8) << 2, 0, 0x400);
            const uint8 *r = v9918_render_sprites(s, scanline);
            if (r) {
                r--; /* invalid, but we advance it before accessing! */
                int x;
                for (x = 32; x--; t++) {
                    const int k = *(src + ((int)(*t) << 3));
                    const int j = coltab[(*t) >> 3];
                    const PIXEL_TYPE bc = COLOR(j & 0x0f);
                    const PIXEL_TYPE fc = COLOR(j >> 4);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x80) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x40) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x20) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x10) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x08) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x04) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x02) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x01) ? fc : bc));
                }
            } else {
                int x;
                for (x = 32; x--; t++) {
                    const int k = *(src + ((int)(*t) << 3));
                    const int j = coltab[(*t) >> 3];
                    const PIXEL_TYPE bc = COLOR(j & 0x0f);
                    const PIXEL_TYPE fc = COLOR(j >> 4);
                    COPY_PIXEL(p, (k & 0x80) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x40) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x20) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x10) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x08) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x04) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x02) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x01) ? fc : bc);
                }
            }
        }
        FUNC(v9918_render_borders_)(p, bc, width);
    }
}

static void FUNC(v9918_render2_)(V9918State *s,
                                 int scanline,
                                 PIXEL_TYPE *p,
                                 unsigned int width)
{
    PIXEL_TYPE bc = COLOR(BG_COLOR);
    p = FUNC(v9918_get_line_ptr_)(scanline, p, bc, width);
    if (p) {
        if (BLANK_ENABLE) {
            p = FUNC(v9918_render_color_)(p, bc, width);
        } else {
            scanline -= BORDER_SIZE;
            
            const int clt = ((scanline & 0xc0) << 5) + (scanline & 7);
            const uint8_t *pgt = s->vram + CHRGEN(clt, 0x2000);
            const uint8_t *t = s->vram + CHRTAB((scanline & 0xf8) << 2, 0, 0x400);
            const uint8 *r = v9918_render_sprites(s, scanline);
            if (r) {
                r--; /* invalid, but we advance it before accessing! */
                int x;
                for (x = 32; x--; t++) {
                    const int k = pgt[(*t) << 3];
                    const int i = s->vram[COLTAB(clt + ((*t) << 3), 0x2000)];
                    const PIXEL_TYPE bc = COLOR(i & 0x0f);
                    const PIXEL_TYPE fc = COLOR(i >> 4);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x80) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x40) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x20) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x10) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x08) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x04) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x02) ? fc : bc));
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : ((k & 0x01) ? fc : bc));
                }
            } else {
                int x;
                for (x = 32; x--; t++) {
                    const int k = pgt[(*t) << 3];
                    const int i = s->vram[COLTAB(clt + ((*t) << 3), 0x2000)];
                    const PIXEL_TYPE bc = COLOR(i & 0x0f);
                    const PIXEL_TYPE fc = COLOR(i >> 4);
                    COPY_PIXEL(p, (k & 0x80) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x40) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x20) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x10) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x08) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x04) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x02) ? fc : bc);
                    COPY_PIXEL(p, (k & 0x01) ? fc : bc);
                }
            }
        }
        FUNC(v9918_render_borders_)(p, bc, width);
    }
}

static void FUNC(v9918_render3_)(V9918State *s,
                                 int scanline,
                                 PIXEL_TYPE *p,
                                 unsigned int width)
{
    PIXEL_TYPE bc = COLOR(BG_COLOR);
    p = FUNC(v9918_get_line_ptr_)(scanline, p, bc, width);
    if (p) {
        if (BLANK_ENABLE) {
            p = FUNC(v9918_render_color_)(p, bc, width);
        } else {
            scanline -= BORDER_SIZE;
            
            const uint8_t *cg = s->vram + CHRGEN(0, 0x800);
            const uint8_t *t = s->vram + CHRTAB((scanline & 0xf8) << 2, 0, 0x400);
            const uint8 *r = v9918_render_sprites(s, scanline);
            if (r) {
                r--; /* invalid, but we advance it before accessing! */
                int x;
                for (x = 32; x--; t++) {
                    const uint8_t c = cg[((int)(*t) << 3) + ((scanline >> 2) & 7)];
                    const PIXEL_TYPE c2 = COLOR(c & 0x0f);
                    const PIXEL_TYPE c1 = COLOR(c >> 4);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : c1);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : c1);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : c1);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : c1);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : c2);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : c2);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : c2);
                    COPY_PIXEL(p, (*++r) ? COLOR(*r) : c2);
                }
            } else {
                int x;
                for (x = 32; x--; t++) {
                    const uint8_t c = cg[((int)(*t) << 3) + ((scanline >> 2) & 7)];
                    const PIXEL_TYPE c2 = COLOR(c & 0x0f);
                    const PIXEL_TYPE c1 = COLOR(c >> 4);
                    COPY_PIXEL(p, c1); COPY_PIXEL(p, c1);
                    COPY_PIXEL(p, c1); COPY_PIXEL(p, c1);
                    COPY_PIXEL(p, c2); COPY_PIXEL(p, c2);
                    COPY_PIXEL(p, c2); COPY_PIXEL(p, c2);
                }
            }
        }
        FUNC(v9918_render_borders_)(p, bc, width);
    }
}

static v9918_render_fn_t DEPTH_Z_GLUE(v9918_render_fn_)[4] = {
    (v9918_render_fn_t)FUNC(v9918_render0_),
    (v9918_render_fn_t)FUNC(v9918_render1_),
    (v9918_render_fn_t)FUNC(v9918_render2_),
    (v9918_render_fn_t)FUNC(v9918_render3_),
};

#undef COLOR
#undef FUNC
#undef DEPTH_Z_GLUE
#undef DEPTH_GLUE
#undef DEPTH
#undef PIXEL_TYPE
#undef COPY_PIXEL
#undef ADVANCE_PIXELS
#undef RETRACE_PIXELS
