#include <gtk/gtk.h>
#include <glade/glade.h>

#include "gtk.h"
#include "sysemu.h"

//#define DEBUG_GTK

#ifdef DEBUG_GTK
#define dprintf(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define dprintf(fmt, ...) do { } while (0)
#endif

static void gtk_display_update(DisplayState *ds, int x, int y, int w, int h)
{
    dprintf("update (%d, %d)-(%d, %d)\n", x, y, w, h);
}

static void gtk_display_resize(DisplayState *ds)
{
    dprintf("resize\n");
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

void gtk_display_init(DisplayState *ds)
{
    DisplayChangeListener *dcl;
    GladeXML *xml;
    GtkWidget *window, *statusbar, *hbox;
    int ret;

    gtk_init(NULL, NULL);

    dcl = qemu_mallocz(sizeof(*dcl));
    dcl->dpy_update = gtk_display_update;
    dcl->dpy_refresh = gtk_display_refresh;
    dcl->dpy_resize = gtk_display_resize;
    register_displaychangelistener(ds, dcl);

    ret = chdir("/home/anthony/git/qemu/gtk");
    assert(ret > -1);
    xml = glade_xml_new("qemu-gui.glade", NULL, NULL);
    assert(xml != NULL);

    window = glade_xml_get_widget(xml, "window1");
    gtk_widget_show_all(window);

    hbox = glade_xml_get_widget(xml, "hbox1");
    statusbar = glade_xml_get_widget(xml, "statusbar1");

    printf("%p %p\n", hbox, statusbar);
}
