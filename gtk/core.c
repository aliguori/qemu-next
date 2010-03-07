#include <gtk/gtk.h>

#include "gtk.h"
#include "sysemu.h"
#include "gtk/qemudisplay.h"

typedef struct QemuDisplayChangeListener
{
    DisplayChangeListener ops;
    GtkWidget *obj;
    int valid;
} QemuDisplayChangeListener;

static void gtk_display_update(DisplayState *ds, int x, int y, int w, int h)
{
    QemuDisplayChangeListener *dcl = ds->opaque;
    if (dcl->valid) {
        qemu_display_update(QEMU_DISPLAY(dcl->obj), ds, x, y, w, h);
    }
}

static void gtk_display_resize(DisplayState *ds)
{
    QemuDisplayChangeListener *dcl = ds->opaque;
    if (dcl->valid) {
        qemu_display_resize(QEMU_DISPLAY(dcl->obj), ds);
    }
}

static void gtk_display_refresh(DisplayState *ds)
{
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    vga_hw_invalidate();
    vga_hw_update();
}

static gboolean close_window(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    qemu_system_shutdown_request();
    return FALSE;
}

static void display_unrealize(GtkWidget *widget, gpointer data)
{
    QemuDisplayChangeListener *dcl = data;
    dcl->valid = 0;
}

void gtk_display_init(DisplayState *ds)
{
    GtkWidget *window, *display, *frame;
    GtkBuilder *builder;
    int ret;
    char *gtk_path;
    QemuDisplayChangeListener *dcl;

    gtk_init(NULL, NULL);

    /* need to handle this better */
    gtk_path = getenv("HACK_GTK_PATH");
    if (gtk_path) {
        ret = chdir(gtk_path);
    } else {
        ret = chdir("/home/anthony/git/qemu/gtk");
    }
    assert(ret > -1);

    builder = gtk_builder_new();
    gtk_builder_add_from_file(builder, "qemu-gui.xml", NULL);

    display = qemu_display_new();
    gtk_widget_show(display);

    /* FIXME figure out how to integrate custom widgets into glade */
    frame = GTK_WIDGET(gtk_builder_get_object(builder, "frame2"));
    gtk_container_add(GTK_CONTAINER(frame), display);

    dcl = qemu_mallocz(sizeof(*dcl));
    dcl->ops.dpy_update = gtk_display_update;
    dcl->ops.dpy_refresh = gtk_display_refresh;
    dcl->ops.dpy_resize = gtk_display_resize;
    dcl->obj = display;
    dcl->valid = 1;
    ds->opaque = dcl;

    register_displaychangelistener(ds, &dcl->ops);

    /* FIXME should not be necessary */
    gtk_widget_grab_focus(display);

    window = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));

    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(close_window), NULL);
    g_signal_connect(G_OBJECT(display), "unrealize",
                     G_CALLBACK(display_unrealize), dcl);
}
