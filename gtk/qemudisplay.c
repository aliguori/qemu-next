#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>

#include "gtk.h"
#include "sysemu.h"
#include "gtk/qemudisplay.h"
#include "x_keymap.h"

#ifndef _WIN32
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#endif

//#define DEBUG_GTK
#ifdef DEBUG_GTK
#define dprintf(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define dprintf(fmt, ...) do { } while (0)
#endif

struct _QemuDisplayPrivate
{
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

/* Utility functions */

static QemuDisplayPrivate *qemu_display_get_private(QemuDisplay *obj)
{
    return G_TYPE_INSTANCE_GET_PRIVATE(obj, QEMU_TYPE_DISPLAY,
                                       QemuDisplayPrivate);
}

#ifndef _WIN32
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

static uint8_t gdk_keyevent_to_keycode(const GdkEventKey *ev)
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
#else
static uint8_t gdk_keyevent_to_keycode(const GdkEventKey *ev)
{
    return ev->hardware_keycode;
}
#endif

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

static void gtk_display_pointer_set_absolute(QemuDisplay *obj,
                                             gboolean absolute)
{
    QemuDisplayPrivate *da = obj->priv;
    GdkCursor *cursor = NULL;

    da->pointer_is_absolute = absolute;
    if (da->pointer_is_absolute) {
        if (da->null_cursor == NULL) {
            da->null_cursor = create_null_cursor();
        }
        cursor = da->null_cursor;
    }

    gdk_window_set_cursor(GTK_WIDGET(obj)->window, cursor);
}

static gboolean check_absolute(QemuDisplay *obj)
{
    QemuDisplayPrivate *da = obj->priv;
    gboolean absolute;

    absolute = !!kbd_mouse_is_absolute();

    if (absolute != da->pointer_is_absolute) {
        da->pointer_is_absolute = absolute;
        gtk_display_pointer_set_absolute(obj, absolute);
    }

    return absolute;
}

/* Widget event handlers */

static gboolean qemu_display_expose(GtkWidget *widget, GdkEventExpose *expose)
{
    QemuDisplay *obj = QEMU_DISPLAY(widget);
    QemuDisplayPrivate *da = obj->priv;
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

static gboolean qemu_display_key(GtkWidget *widget, GdkEventKey *key)
{
    int keycode;

    keycode = gdk_keyevent_to_keycode(key);

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

static gboolean qemu_display_motion(GtkWidget *widget, GdkEventMotion *motion)
{
    QemuDisplay *obj = QEMU_DISPLAY(widget);
    QemuDisplayPrivate *da = obj->priv;

    if (!GTK_WIDGET_HAS_FOCUS(widget) || !check_absolute(obj)) {
        return FALSE;
    }

    da->pointer_last_x = motion->x * 0x7FFF / (da->width - 1);
    da->pointer_last_y = motion->y * 0x7FFF / (da->height - 1);

    kbd_mouse_event(da->pointer_last_x, da->pointer_last_y, 0, da->button_mask);

    return TRUE;
}

static gboolean qemu_display_button(GtkWidget *widget, GdkEventButton *button)
{
    QemuDisplay *obj = QEMU_DISPLAY(widget);
    QemuDisplayPrivate *da = obj->priv;
    int mask = 0;

    if (!GTK_WIDGET_HAS_FOCUS(widget) || !check_absolute(obj)) {
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

static gboolean qemu_display_scroll(GtkWidget *widget, GdkEventScroll *scroll)
{
    QemuDisplay *obj = QEMU_DISPLAY(widget);
    QemuDisplayPrivate *da = obj->priv;
    int dz;

    if (!GTK_WIDGET_HAS_FOCUS(widget) || !check_absolute(obj)) {
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

static gboolean qemu_display_focus(GtkWidget *widget, GdkEventFocus *focus)
{
    QemuDisplay *obj = QEMU_DISPLAY(widget);
    QemuDisplayPrivate *da = obj->priv;

    if (focus->in) {
        gtk_display_pointer_set_absolute(obj, da->pointer_is_absolute);
    } else {
        gdk_window_set_cursor(widget->window, NULL);
    }

    return FALSE;
}

/* G_TYPE boiler plate code */

G_DEFINE_TYPE(QemuDisplay, qemu_display, GTK_TYPE_DRAWING_AREA)

static void qemu_display_finalize(GObject *gobject)
{
    QemuDisplay *obj = QEMU_DISPLAY(gobject);
    QemuDisplayPrivate *da = obj->priv;

    if (da->pixbuf) {
        g_object_unref(da->pixbuf);
        da->pixbuf = NULL;
    }
    if (da->gc) {
        g_object_unref(da->gc);
        da->gc = NULL;
    }
    if (da->null_cursor) {
        g_object_unref(da->null_cursor);
        da->null_cursor = NULL;
    }
}

static void qemu_display_init(QemuDisplay *obj)
{
    GtkWidget *widget = GTK_WIDGET(obj);
    QemuDisplayPrivate *da;
    GdkEventMask events;

    events = GDK_POINTER_MOTION_MASK |
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_BUTTON_MOTION_MASK |
        GDK_ENTER_NOTIFY_MASK |
        GDK_LEAVE_NOTIFY_MASK |
        GDK_SCROLL_MASK |
        GDK_KEY_PRESS_MASK;

    g_object_set(G_OBJECT(widget),
                 "can-focus", TRUE,
                 "has-focus", TRUE,
                 "double-buffered", FALSE,
                 "events", events,
                 NULL);

    da = obj->priv = qemu_display_get_private(obj);

    da->pixbuf = NULL;
    da->gc = NULL;
    da->width = -1;
    da->height = -1;
    da->button_mask = 0;
    da->null_cursor = NULL;
    da->pointer_is_absolute = FALSE;
}

static void qemu_display_class_init(QemuDisplayClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *gtkwidget_class = GTK_WIDGET_CLASS(klass);

    gtkwidget_class->expose_event = qemu_display_expose;
    gtkwidget_class->key_press_event = qemu_display_key;
    gtkwidget_class->key_release_event = qemu_display_key;
    gtkwidget_class->motion_notify_event = qemu_display_motion;
    gtkwidget_class->button_press_event = qemu_display_button;
    gtkwidget_class->button_release_event = qemu_display_button;
    gtkwidget_class->scroll_event = qemu_display_scroll;
    gtkwidget_class->focus_in_event = qemu_display_focus;
    gtkwidget_class->focus_out_event = qemu_display_focus;

    object_class->finalize = qemu_display_finalize;

    g_type_class_add_private(klass, sizeof(QemuDisplayPrivate));
}

/* Public functions */

GtkWidget *qemu_display_new(void)
{
    return GTK_WIDGET(g_object_new(QEMU_TYPE_DISPLAY, NULL));
}

void qemu_display_update(QemuDisplay *obj, DisplayState *ds,
                         gint x, gint y, gint w, gint h)
{
    QemuDisplayPrivate *da = obj->priv;
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

    gtk_widget_queue_draw_area(GTK_WIDGET(obj), x, y, w + 1, h + 1);
}

void qemu_display_resize(QemuDisplay *obj, DisplayState *ds)
{
    QemuDisplayPrivate *da = obj->priv;

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

    gtk_widget_set_size_request(GTK_WIDGET(obj), da->width, da->height);
}
