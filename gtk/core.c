#include <gtk/gtk.h>

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

static void close_window(void)
{
    exit(0);
}

void gtk_display_init(DisplayState *ds)
{
    GtkWidget *window, *drawing_area;
    GtkBuilder *builder;
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
    builder = gtk_builder_new();
    gtk_builder_add_from_file(builder, "qemu-gui.xml", NULL);

    drawing_area = GTK_WIDGET(gtk_builder_get_object(builder, "drawingarea1"));
    gtk_display_setup_drawing_area(drawing_area, ds);

    window = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));

    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(close_window), NULL);
}
