#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>

#include "gtk.h"
#include "sysemu.h"
#include "gtk/drawingarea.h"
#include "x_keymap.h"

#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

//#define DEBUG_GTK
#ifdef DEBUG_GTK
#define dprintf(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define dprintf(fmt, ...) do { } while (0)
#endif

static int check_for_evdev(void)
{
    XkbDescPtr desc = NULL;
    char *keycodes = NULL;
    static int has_evdev = -1;

    if (has_evdev == -1) {
        has_evdev = 0;
    } else {
        return has_evdev;
    }

    desc = XkbGetKeyboard(GDK_DISPLAY(),
                          XkbGBN_AllComponentsMask,
                          XkbUseCoreKbd);
    if (desc && desc->names) {
        keycodes = XGetAtomName(GDK_DISPLAY(), desc->names->keycodes);
        if (keycodes == NULL) {
            fprintf(stderr, "could not lookup keycode name\n");
        } else if (strstart(keycodes, "evdev", NULL)) {
            has_evdev = 1;
        } else if (!strstart(keycodes, "xfree86", NULL)) {
            fprintf(stderr, "unknown keycodes `%s', please report to "
                    "qemu-devel@nongnu.org\n", keycodes);
        }
    }

    if (desc) {
        XkbFreeKeyboard(desc, XkbGBN_AllComponentsMask, True);
    }
    if (keycodes) {
        XFree(keycodes);
    }
    return has_evdev;
}

static uint8_t gtk_keyevent_to_keycode(const GdkEventKey *ev)
{
    int keycode;
    static int has_evdev = -1;

    if (has_evdev == -1)
        has_evdev = check_for_evdev();

    keycode = ev->hardware_keycode;

    if (keycode < 9) {
        keycode = 0;
    } else if (keycode < 97) {
        keycode -= 8; /* just an offset */
    } else if (keycode < 158) {
        /* use conversion table */
        if (has_evdev)
            keycode = translate_evdev_keycode(keycode - 97);
        else
            keycode = translate_xfree86_keycode(keycode - 97);
    } else if (keycode == 208) { /* Hiragana_Katakana */
        keycode = 0x70;
    } else if (keycode == 211) { /* backslash */
        keycode = 0x73;
    } else {
        keycode = 0;
    }
    return keycode;
}

static void gtk_display_update(DisplayState *ds, int x, int y, int w, int h)
{
    QemuGtkDrawingArea *da = ds->opaque;
    PixelFormat *pf = &ds->surface->pf;
    guchar *src_row, *dst_row;
    int j;

    dprintf("update (%d, %d)-(%d, %d)\n", x, y, w, h);

    src_row = ds_get_data(ds) +
        ds_get_linesize(ds) * y +
        ds_get_bytes_per_pixel(ds) * x;

    dst_row = gdk_pixbuf_get_pixels(da->pixbuf) +
        gdk_pixbuf_get_rowstride(da->pixbuf) * y +
        3 * x;

    for (j = 0; j < h; j++) {
        guchar *src, *dst;
        int i;

        src = src_row;
        dst = dst_row;

        for (i = 0; i < w; i++) {
            uint32_t pixel = 0;

            switch (ds_get_bits_per_pixel(ds)) {
            case 8:
                pixel = *(uint8_t *)src;
                break;
            case 16:
                pixel = *(uint16_t *)src;
                break;
            case 32:
                pixel = *(uint32_t *)src;
                break;
            }

            dst[0] = ((pixel & pf->rmask) >> pf->rshift) << (8 - pf->rbits);
            dst[1] = ((pixel & pf->gmask) >> pf->gshift) << (8 - pf->gbits);
            dst[2] = ((pixel & pf->bmask) >> pf->bshift) << (8 - pf->bbits);

            src += ds_get_bytes_per_pixel(ds);
            dst += 3;
        }

        src_row += ds_get_linesize(ds);
        dst_row += gdk_pixbuf_get_rowstride(da->pixbuf);
    }

    gtk_widget_queue_draw_area(da->parent, x, y, w + 1, h + 1);
}

static void gtk_display_resize(DisplayState *ds)
{
    QemuGtkDrawingArea *da = ds->opaque;
    int w, h;

    w = ds_get_width(ds);
    h = ds_get_height(ds);

    dprintf("resize (%d, %d)\n", w, h, ds_get_bits_per_pixel(ds));

    if (da->pixbuf) {
        gdk_pixbuf_unref(da->pixbuf);
        da->pixbuf = NULL;
    }
    da->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);

    gtk_widget_set_size_request(da->parent, w, h);
}

static void gtk_display_refresh(DisplayState *ds)
{
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    vga_hw_invalidate();
    vga_hw_update();
}

static gboolean gtk_display_expose(GtkWidget *widget, GdkEventExpose *expose,
                                   gpointer data)
{
    QemuGtkDrawingArea *da = data;
    int x, y, w, h, pw, ph;

    if (da->gc == NULL) {
        da->gc = gdk_gc_new(widget->window);
    }

    pw = gdk_pixbuf_get_width(da->pixbuf);
    ph = gdk_pixbuf_get_height(da->pixbuf);

    x = MIN(expose->area.x, pw);
    y = MIN(expose->area.y, ph);
    w = MIN(expose->area.x + expose->area.width, pw) - x;
    h = MIN(expose->area.x + expose->area.width, ph) - y;

    if (x != w || y != h) {
        gdk_pixbuf_render_to_drawable(da->pixbuf,
                                      widget->window,
                                      da->gc,
                                      x, y,
                                      x, y,
                                      w, h,
                                      0, 0, 0);
    }

    return TRUE;
}

static gboolean gtk_display_key(GtkWidget *widget, GdkEventKey *key,
                                gpointer data)
{
    int keycode;

    keycode = gtk_keyevent_to_keycode(key);

    if ((keycode & 0x80)) {
        kbd_put_keycode(0xe0);
    }

    if (key->type == GDK_KEY_PRESS) {
        kbd_put_keycode(keycode | 0x80);
    } else {
        kbd_put_keycode(keycode & 0x7F);
    }

    return TRUE;
}

QemuGtkDrawingArea *gtk_display_setup_drawing_area(GtkWidget *drawing_area,
                                                   DisplayState *ds)
{
    DisplayChangeListener *dcl;
    QemuGtkDrawingArea *da;

    da = qemu_malloc(sizeof(*da));

    da->parent = drawing_area;
    da->pixbuf = NULL;
    da->gc = NULL;

    ds->opaque = da;

    dcl = qemu_mallocz(sizeof(*dcl));
    dcl->dpy_update = gtk_display_update;
    dcl->dpy_refresh = gtk_display_refresh;
    dcl->dpy_resize = gtk_display_resize;
    register_displaychangelistener(ds, dcl);

    g_signal_connect(G_OBJECT(drawing_area), "expose-event",
                     G_CALLBACK(gtk_display_expose), da);
    g_signal_connect(G_OBJECT(drawing_area), "key-press-event",
                     G_CALLBACK(gtk_display_key), da);
    g_signal_connect(G_OBJECT(drawing_area), "key-release-event",
                     G_CALLBACK(gtk_display_key), da);

    GTK_WIDGET_SET_FLAGS(drawing_area, GTK_CAN_FOCUS);

    gtk_widget_add_events(drawing_area,
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_BUTTON_MOTION_MASK |
                          GDK_KEY_PRESS_MASK);
    return da;
}
