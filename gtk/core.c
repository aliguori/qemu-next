#include <gtk/gtk.h>
#include <glade/glade.h>

#include "gtk.h"
#include "sysemu.h"
#include "gtk/drawingarea.h"

/* grab the drawing area widget, and start rendering to it.  we'll figure out
 * the button box problem later.
 *
 * 1) render to drawing area (X)
 * 2) accept input from drawing area (X)
 * 3) register icons properly
 * 3.5) hook up mouse
 * 3.6) tie mouse grab and keyboard to status icons
 * 4) figure out how to display them with glade (X)
 * 5) make icons change based on VM activity
 * 6) plumb up menu options
 * 7) windows portability
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

static void close_window(void)
{
    exit(0);
}

void gtk_display_init(DisplayState *ds)
{
    GtkWidget *window, *drawing_area;
    GladeXML *xml;
    int ret;
    char *gtk_path;

    gtk_init(NULL, NULL);

    /* need to handle this better */
    gtk_path = getenv("HACK_GTK_PATH");
    if (gtk_path) {
        ret = chdir(gtk_path);
    } else {
        ret = chdir("/home/anthony/git/qemu/gtk");
    }

    assert(ret > -1);
    xml = glade_xml_new("qemu-gui.glade", NULL, NULL);
    assert(xml != NULL);

    fixup_statusbar(xml);

    drawing_area = glade_xml_get_widget(xml, "drawingarea1");
    gtk_display_setup_drawing_area(drawing_area, ds);

    window = glade_xml_get_widget(xml, "window1");

    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(close_window), NULL);

    gtk_widget_show_all(window);
}
