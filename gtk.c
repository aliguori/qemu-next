#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>

#include "gtk.h"
#include "sysemu.h"

//#define DEBUG_GTK

#ifdef DEBUG_GTK
#define dprintf(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define dprintf(fmt, ...) do { } while (0)
#endif

typedef struct QemuGtkDrawingArea
{
    GtkWidget *parent;
    GdkPixbuf *pixbuf;
    GdkGC *gc;
} QemuGtkDrawingArea;

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

/* grab the drawing area widget, and start rendering to it.  we'll figure out
 * the button box problem later.
 *
 * 1) render to drawing area
 * 2) accept input from drawing area
 * 3) register icons properly
 * 4) figure out how to display them with glade
 * 5) make icons change based on VM activity
 * 6) plumb up menu options
 */

/* In 2.20, we can use gtk_statusbar_get_message_area() to retrieve an hbox
 * that we can pack with widgets.  In fact, glade3 already supports this via
 * XML.  However, because we don't want to duplicate the XML and we need to
 * support older versions of gtk, we have to separate out the status area
 * into a separate section of the XML.
 *
 * That means in this code, we should detect 2.20 and use message_area when
 * we can.
 */
static void fixup_statusbar(GladeXML *xml)
{

    GtkWidget *statusbar, *hbox, *frame;
    GtkShadowType shadow_type;

    hbox = glade_xml_get_widget(xml, "hbox1");
    statusbar = glade_xml_get_widget(xml, "statusbar1");

    /* not perfect, but until 2.20, we can't do it in a better way */
    gtk_widget_style_get(GTK_WIDGET(statusbar), "shadow-type",
                         &shadow_type, NULL);
    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), shadow_type);
    gtk_container_add(GTK_CONTAINER(frame), hbox);
    gtk_widget_show(frame);

    gtk_box_pack_end(GTK_BOX(statusbar), frame, FALSE, TRUE, 0);
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

static void gtk_display_setup_drawing_area(GtkWidget *drawing_area,
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
}

void gtk_display_init(DisplayState *ds)
{
    GtkWidget *window, *drawing_area;
    GladeXML *xml;
    int ret;

    gtk_init(NULL, NULL);

    ret = chdir("/home/anthony/git/qemu/gtk");
    assert(ret > -1);
    xml = glade_xml_new("qemu-gui.glade", NULL, NULL);
    assert(xml != NULL);

    fixup_statusbar(xml);

    drawing_area = glade_xml_get_widget(xml, "drawingarea1");
    gtk_display_setup_drawing_area(drawing_area, ds);

    window = glade_xml_get_widget(xml, "window1");
    gtk_widget_show_all(window);
}
