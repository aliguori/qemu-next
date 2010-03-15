#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "gtk.h"
#include "sysemu.h"
#include "gtk/qemudisplay.h"
#include "gtk/view/autoDrawer.h"

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

    g_object_get(G_OBJECT(obj), "grab", &grab_active, NULL);
    g_object_set(G_OBJECT(obj), "grab", !grab_active, NULL);
}

static gboolean enter_grab(QemuDisplay *obj, gpointer data)
{
    return FALSE;
}

static void leave_grab(QemuDisplay *obj, gpointer data)
{
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
 *
 *  - Create a widget by subclassing GtkImage and implementing GtkActivable
 *    that can be used to represent the status icons.  We can then associate
 *    GtkActions with the widget and use the tool tips associated with the
 *    Actions to display to the user.
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

static GtkWidget *replace_vbox_with_autodrawer(GtkWidget *vbox)
{
    GtkWidget *parent, *drawer;
    GtkBoxChild *box_child;
    GtkBoxChild child[2];
    GtkBox *box = GTK_BOX(vbox);
    int i;
    gboolean expand, fill;
    guint padding;
    GtkPackType pack_type;
    guint position;

    g_assert(g_list_length(box->children) == 2);

    for (i = 0; i < 2; i++) {
        box_child = g_list_nth_data(box->children, i);
        memcpy(&child[i], box_child, sizeof(child[i]));
    }

    parent = gtk_widget_get_parent(vbox);
    drawer = ViewAutoDrawer_New();

    for (i = 0; i < 2; i++) {
        g_object_ref(child[i].widget);
        gtk_container_remove(GTK_CONTAINER(vbox), child[i].widget);

        g_object_get(G_OBJECT(child[i].widget), "position", &position, NULL);
        if (position == 0) {
            ViewOvBox_SetOver(VIEW_OV_BOX(drawer), child[i].widget);
        } else {
            ViewOvBox_SetUnder(VIEW_OV_BOX(drawer), child[i].widget);
        }
        g_object_unref(child[i].widget);
    }

    g_object_get(G_OBJECT(vbox), "position", &position, NULL);
    gtk_box_query_child_packing(GTK_BOX(parent), vbox,
                                &expand, &fill, &padding, &pack_type);
    gtk_container_remove(GTK_CONTAINER(parent), vbox);

    if (pack_type == GTK_PACK_START) {
        gtk_box_pack_start(GTK_BOX(parent), drawer, expand, fill, padding);
    } else {
        gtk_box_pack_end(GTK_BOX(parent), drawer, expand, fill, padding);
    }

    gtk_box_reorder_child(GTK_BOX(parent), drawer, position);

    return drawer;
}

void gtk_display_init(DisplayState *ds)
{
    GtkWidget *window, *display, *vbox2, *drawer;
    GtkBuilder *builder;
    int ret;
    char *gtk_path;
    QemuDisplayChangeListener *dcl;
    GError *error = NULL;

    gtk_init(NULL, NULL);

    /* need to handle this better */
    gtk_path = getenv("HACK_GTK_PATH");
    if (gtk_path) {
        ret = chdir(gtk_path);
    } else {
        ret = chdir("/home/anthony/git/qemu/gtk");
    }
    assert(ret > -1);

    qemu_display_get_type();

    builder = gtk_builder_new();
    gtk_builder_add_from_file(builder, "qemu-gui.xml", &error);
    if (error != NULL) {
        printf("%s\n", error->message);
        exit(1);
    }

    vbox2 = GTK_WIDGET(gtk_builder_get_object(builder, "vbox2"));
    display = GTK_WIDGET(gtk_builder_get_object(builder, "display"));

    drawer = replace_vbox_with_autodrawer(vbox2);
    ViewAutoDrawer_SetActive(VIEW_AUTODRAWER(drawer), FALSE);

    qemu_display_set_host_key(QEMU_DISPLAY(display),
                              3, (gint[]){GDK_Control_L, GDK_Alt_L, GDK_a});
    qemu_display_set_click_to_grab(QEMU_DISPLAY(display), TRUE);

    dcl = qemu_mallocz(sizeof(*dcl));
    dcl->ops.dpy_update = gtk_display_update;
    dcl->ops.dpy_refresh = gtk_display_refresh;
    dcl->ops.dpy_resize = gtk_display_resize;
    dcl->obj = display;
    dcl->valid = 1;
    ds->opaque = dcl;

    register_displaychangelistener(ds, &dcl->ops);

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

    window = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));
    gtk_widget_show_all(window);

    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(close_window), NULL);
}
