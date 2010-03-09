#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

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

static void host_key_event(QemuDisplay *obj, gpointer data)
{
    gboolean grab_active;

    printf("host key event\n");

    g_object_get(G_OBJECT(obj), "grab", &grab_active, NULL);
    g_object_set(G_OBJECT(obj), "grab", !grab_active, NULL);
}

static gboolean enter_grab(QemuDisplay *obj, gpointer data)
{
    printf("enter grab\n");

    return FALSE;
}

static void leave_grab(QemuDisplay *obj, gpointer data)
{
    printf("leave grab\n");
}

/* TODO
 *  - Display tool tips describing mouse state
 *  - Use GtkAction to represent mouse states?
 *  - Display popup menu on right click containing:
 * 
 *     o QEMU PS/2 Mouse
 *     X QEMU USB Tablet
 *     ---------------------
 *     Edit input devices...
 */

static void update_mouse_icon(QemuDisplay *obj, gpointer data)
{
    GtkBuilder *builder = data;
    GtkWidget *icon;
    gboolean relative_pointer;
    const char *stock_id;

    icon = GTK_WIDGET(gtk_builder_get_object(builder, "image4"));

    g_object_get(G_OBJECT(obj),
                 "relative-pointer", &relative_pointer,
                 NULL);

    if (relative_pointer) {
        if (kbd_mouse_has_absolute()) {
            stock_id = "mouse-can-seamless";
        } else {
            stock_id = "mouse";
        }
    } else {
        stock_id = "mouse-seamless";
    }

    gtk_image_set_from_stock(GTK_IMAGE(icon), stock_id, GTK_ICON_SIZE_MENU);
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

    {
        GValueArray *host_key = g_value_array_new(3);
        GValue value;

        memset(&value, 0, sizeof(value));
        g_value_init(&value, G_TYPE_INT);
        g_value_set_int(&value, GDK_Control_L);
        g_value_array_append(host_key, &value);

        memset(&value, 0, sizeof(value));
        g_value_init(&value, G_TYPE_INT);
        g_value_set_int(&value, GDK_Alt_L);
        g_value_array_append(host_key, &value);

        memset(&value, 0, sizeof(value));
        g_value_init(&value, G_TYPE_INT);
        g_value_set_int(&value, GDK_a);
        g_value_array_append(host_key, &value);

        g_object_set(G_OBJECT(display),
                     "host-key", host_key,
                     "click-to-grab", TRUE,
                     NULL);
    }

    window = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));

    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(close_window), NULL);
    g_signal_connect(G_OBJECT(display), "unrealize",
                     G_CALLBACK(display_unrealize), dcl);

    g_signal_connect(G_OBJECT(display), "host-key-event",
                     G_CALLBACK(host_key_event), NULL);

    g_signal_connect(G_OBJECT(display), "enter-grab-event",
                     G_CALLBACK(enter_grab), NULL);
    g_signal_connect(G_OBJECT(display), "leave-grab-event",
                     G_CALLBACK(leave_grab), NULL);

    g_signal_connect(G_OBJECT(display), "relative-pointer-event",
                     G_CALLBACK(update_mouse_icon), builder);
    g_signal_connect(G_OBJECT(display), "absolute-pointer-event",
                     G_CALLBACK(update_mouse_icon), builder);

    update_mouse_icon(QEMU_DISPLAY(display), builder);
}
