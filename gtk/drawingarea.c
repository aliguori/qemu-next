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

struct QemuGtkDrawingArea
{
    GtkWidget *parent;
    GdkPixbuf *pixbuf;
    GdkGC *gc;
    gint width;
    gint height;
    gint pointer_last_x;
    gint pointer_last_y;
    gint button_mask;
    GdkCursor *null_cursor;
    gboolean pointer_is_absolute;
};

/* DisplayState interfaces */

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

    da->width = ds_get_width(ds);
    da->height = ds_get_height(ds);

    dprintf("resize (%d, %d)\n",
            da->width, da->height,
            ds_get_bits_per_pixel(ds));

    if (da->pixbuf) {
        gdk_pixbuf_unref(da->pixbuf);
        da->pixbuf = NULL;
    }
    da->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                da->width, da->height);

    gtk_widget_set_size_request(da->parent, da->width, da->height);
}

static void gtk_display_refresh(DisplayState *ds)
{
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    vga_hw_invalidate();
    vga_hw_update();
}

/* Widget events */

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

static GdkCursor *create_null_cursor(void)
{
    GdkBitmap *image;
    gchar data[4] = {0};
    GdkColor fg = { 0, 0, 0, 0 };
    GdkCursor *cursor;

    image = gdk_bitmap_create_from_data(NULL, data, 1, 1);

    cursor = gdk_cursor_new_from_pixmap(GDK_PIXMAP(image),
                                        GDK_PIXMAP(image),
                                        &fg, &fg, 0, 0);
    g_object_unref(image);

    return cursor;
}

static void gtk_display_pointer_set_absolute(QemuGtkDrawingArea *da,
                                             gboolean absolute)
{
    GdkCursor *cursor = NULL;

    da->pointer_is_absolute = absolute;
    if (da->pointer_is_absolute) {
        if (da->null_cursor == NULL) {
            da->null_cursor = create_null_cursor();
        }
        cursor = da->null_cursor;
    }

    gdk_window_set_cursor(da->parent->window, cursor);
}

static gboolean check_absolute(QemuGtkDrawingArea *da)
{
    gboolean absolute;

    absolute = !!kbd_mouse_is_absolute();

    if (absolute != da->pointer_is_absolute) {
        da->pointer_is_absolute = absolute;
        gtk_display_pointer_set_absolute(da, absolute);
    }

    return absolute;
}

static gboolean gtk_display_motion(GtkWidget *widget, GdkEventMotion *motion,
                                   gpointer opaque)
{
    QemuGtkDrawingArea *da = opaque;

    if (!GTK_WIDGET_HAS_FOCUS(widget) || !check_absolute(da)) {
        return FALSE;
    }

    da->pointer_last_x = motion->x * 0x7FFF / (da->width - 1);
    da->pointer_last_y = motion->y * 0x7FFF / (da->height - 1);

    kbd_mouse_event(da->pointer_last_x, da->pointer_last_y, 0, da->button_mask);

    return TRUE;
}

static gboolean gtk_display_button(GtkWidget *widget, GdkEventButton *button,
                                   gpointer opaque)
{
    QemuGtkDrawingArea *da = opaque;
    int mask = 0;

    if (!GTK_WIDGET_HAS_FOCUS(widget) || !check_absolute(da)) {
        return FALSE;
    }

    switch (button->button) {
    case 1:
        mask = MOUSE_EVENT_LBUTTON;
        break;
    case 2:
        mask = MOUSE_EVENT_MBUTTON;
        break;
    case 3:
        mask = MOUSE_EVENT_RBUTTON;
        break;
    }

    if (button->type == GDK_BUTTON_PRESS) {
        da->button_mask |= mask;
    } else if (button->type == GDK_BUTTON_RELEASE) {
        da->button_mask &= ~mask;
    }

    kbd_mouse_event(da->pointer_last_x, da->pointer_last_y, 0, da->button_mask);

    return TRUE;
}

static gboolean gtk_display_scroll(GtkWidget *widget, GdkEventScroll *scroll,
                                   gpointer opaque)
{
    QemuGtkDrawingArea *da = opaque;
    int dz;

    if (!GTK_WIDGET_HAS_FOCUS(widget) || !check_absolute(da)) {
        return FALSE;
    }

    switch (scroll->direction) {
    case GDK_SCROLL_UP:
        dz = -1;
        break;
    case GDK_SCROLL_DOWN:
        dz = +1;
        break;
    default:
        dz = 0;
        break;
    }

    kbd_mouse_event(da->pointer_last_x, da->pointer_last_y,
                    dz, da->button_mask);

    return TRUE;
}

static gboolean gtk_display_focus(GtkWidget *widget, GdkEventFocus *focus,
                                  gpointer opaque)
{
    QemuGtkDrawingArea *da = opaque;

    if (focus->in) {
        gtk_display_pointer_set_absolute(da, da->pointer_is_absolute);
    } else {
        gdk_window_set_cursor(da->parent->window, NULL);
    }

    return FALSE;
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
    da->width = -1;
    da->height = -1;
    da->button_mask = 0;
    da->null_cursor = NULL;
    da->pointer_is_absolute = FALSE;

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
    g_signal_connect(G_OBJECT(drawing_area), "motion-notify-event",
                     G_CALLBACK(gtk_display_motion), da);
    g_signal_connect(G_OBJECT(drawing_area), "button-press-event",
                     G_CALLBACK(gtk_display_button), da);
    g_signal_connect(G_OBJECT(drawing_area), "button-release-event",
                     G_CALLBACK(gtk_display_button), da);
    g_signal_connect(G_OBJECT(drawing_area), "scroll-event",
                     G_CALLBACK(gtk_display_scroll), da);
    g_signal_connect(G_OBJECT(drawing_area), "focus-in-event",
                     G_CALLBACK(gtk_display_focus), da);
    g_signal_connect(G_OBJECT(drawing_area), "focus-out-event",
                     G_CALLBACK(gtk_display_focus), da);

    return da;
}
