#ifndef QEMU_DISPLAY_H
#define QEMU_DISPLAY_H

#include "console.h"

/**
 * This represents a backend capable of displaying graphics.  This may be the
 * VNC server, an SDL window, etc.
 *
 * All DisplayChangeListeners are associated with a DisplayState.  There may be
 * many DCL's per DS, but only one DS per DCL.
 */
struct DisplayChangeListener {
    int idle;
    uint64_t gui_timer_interval;
    DisplayState *ds;

    void (*dpy_update)(DisplayChangeListener *dcl, int x, int y, int w, int h);
    void (*dpy_resize)(DisplayChangeListener *dcl);
    void (*dpy_setdata)(DisplayChangeListener *dcl);
    void (*dpy_refresh)(DisplayChangeListener *dcl);
    void (*dpy_copy)(DisplayChangeListener *dcl, int src_x, int src_y,
                     int dst_x, int dst_y, int w, int h);
    void (*dpy_fill)(DisplayChangeListener *dcl, int x, int y,
                     int w, int h, uint32_t c);
    void (*dpy_text_cursor)(DisplayChangeListener *dcl, int x, int y);

    void (*mouse_set)(DisplayChangeListener *dcl, int x, int y, int on);
    void (*cursor_define)(DisplayChangeListener *dcl, QEMUCursor *cursor);

    struct DisplayChangeListener *next;
};

/**
 * Under the right circumstances, we can avoid bouncing by using a backend
 * allocated buffer to render directly to assuming the backend can create a
 * buffer and that there is only one backend for a given DisplayState that wants
 * to be the allocator.  This is backend state.
 */
struct DisplayAllocator {
    DisplaySurface* (*create_displaysurface)(DisplayAllocator *da, int width, int height);
    DisplaySurface* (*resize_displaysurface)(DisplayAllocator *da, DisplaySurface *surface, int width, int height);
    void (*free_displaysurface)(DisplayAllocator *da, DisplaySurface *surface);
};

/**
 * This is associated with a device.
 */
struct DisplayState {
    struct DisplaySurface *surface;
    struct QEMUTimer *gui_timer;

    struct DisplayAllocator* allocator;
    struct DisplayChangeListener* listeners;
};

void register_displaystate(DisplayState *ds);
DisplayState *get_displaystate(void);
DisplaySurface* qemu_create_displaysurface_from(int width, int height, int bpp,
                                                int linesize, uint8_t *data);
void qemu_alloc_display(DisplaySurface *surface, int width, int height,
                        int linesize, PixelFormat pf, int newflags);
PixelFormat qemu_different_endianness_pixelformat(int bpp);
PixelFormat qemu_default_pixelformat(int bpp);

DisplayAllocator *register_displayallocator(DisplayState *ds, DisplayAllocator *da);

static inline DisplaySurface* qemu_create_displaysurface(DisplayState *ds, int width, int height)
{
    return ds->allocator->create_displaysurface(ds->allocator, width, height);
}

static inline DisplaySurface* qemu_resize_displaysurface(DisplayState *ds, int width, int height)
{
    return ds->allocator->resize_displaysurface(ds->allocator, ds->surface, width, height);
}

static inline void qemu_free_displaysurface(DisplayState *ds)
{
    ds->allocator->free_displaysurface(ds->allocator, ds->surface);
}

static inline int is_surface_bgr(DisplaySurface *surface)
{
    if (surface->pf.bits_per_pixel == 32 && surface->pf.rshift == 0)
        return 1;
    else
        return 0;
}

static inline int is_buffer_shared(DisplaySurface *surface)
{
    return (!(surface->flags & QEMU_ALLOCATED_FLAG) &&
            !(surface->flags & QEMU_REALPIXELS_FLAG));
}

static inline void register_displaychangelistener(DisplayState *ds, DisplayChangeListener *dcl)
{
    dcl->next = ds->listeners;
    ds->listeners = dcl;
}

static inline void dpy_update(DisplayState *s, int x, int y, int w, int h)
{
    struct DisplayChangeListener *dcl = s->listeners;
    while (dcl != NULL) {
        dcl->dpy_update(dcl, x, y, w, h);
        dcl = dcl->next;
    }
}

static inline void dpy_resize(DisplayState *s)
{
    struct DisplayChangeListener *dcl = s->listeners;
    while (dcl != NULL) {
        dcl->dpy_resize(dcl);
        dcl = dcl->next;
    }
}

static inline void dpy_setdata(DisplayState *s)
{
    struct DisplayChangeListener *dcl = s->listeners;
    while (dcl != NULL) {
        if (dcl->dpy_setdata) dcl->dpy_setdata(dcl);
        dcl = dcl->next;
    }
}

static inline void dpy_refresh(DisplayState *s)
{
    struct DisplayChangeListener *dcl = s->listeners;
    while (dcl != NULL) {
        if (dcl->dpy_refresh) dcl->dpy_refresh(dcl);
        dcl = dcl->next;
    }
}

static inline void dpy_copy(struct DisplayState *s, int src_x, int src_y,
                             int dst_x, int dst_y, int w, int h) {
    struct DisplayChangeListener *dcl = s->listeners;
    while (dcl != NULL) {
        if (dcl->dpy_copy)
            dcl->dpy_copy(dcl, src_x, src_y, dst_x, dst_y, w, h);
        else /* TODO */
            dcl->dpy_update(dcl, dst_x, dst_y, w, h);
        dcl = dcl->next;
    }
}

static inline void dpy_fill(struct DisplayState *s, int x, int y,
                             int w, int h, uint32_t c) {
    struct DisplayChangeListener *dcl = s->listeners;
    while (dcl != NULL) {
        if (dcl->dpy_fill) dcl->dpy_fill(dcl, x, y, w, h, c);
        dcl = dcl->next;
    }
}

static inline void dpy_cursor(struct DisplayState *s, int x, int y) {
    struct DisplayChangeListener *dcl = s->listeners;
    while (dcl != NULL) {
        if (dcl->dpy_text_cursor) dcl->dpy_text_cursor(dcl, x, y);
        dcl = dcl->next;
    }
}

static inline void dpy_cursor_define(DisplayState *s, QEMUCursor *c)
{
    struct DisplayChangeListener *dcl = s->listeners;
    while (dcl != NULL) {
        if (dcl->cursor_define) dcl->cursor_define(dcl, c);
        dcl = dcl->next;
    }
}

static inline void dpy_mouse_set(DisplayState *s, int x, int y, int on)
{
    struct DisplayChangeListener *dcl = s->listeners;
    while (dcl != NULL) {
        if (dcl->mouse_set) dcl->mouse_set(dcl, x, y, on);
        dcl = dcl->next;
    }
}

static inline int ds_get_linesize(DisplayState *ds)
{
    return ds->surface->linesize;
}

static inline uint8_t* ds_get_data(DisplayState *ds)
{
    return ds->surface->data;
}

static inline int ds_get_width(DisplayState *ds)
{
    return ds->surface->width;
}

static inline int ds_get_height(DisplayState *ds)
{
    return ds->surface->height;
}

static inline int ds_get_bits_per_pixel(DisplayState *ds)
{
    return ds->surface->pf.bits_per_pixel;
}

static inline int ds_get_bytes_per_pixel(DisplayState *ds)
{
    return ds->surface->pf.bytes_per_pixel;
}

#endif