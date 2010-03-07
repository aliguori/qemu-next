#ifndef QEMU_GTK_DRAWING_AREA_H
#define QEMU_GTK_DRAWING_AREA_H

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>

typedef struct QemuGtkDrawingArea QemuGtkDrawingArea;

QemuGtkDrawingArea *gtk_display_setup_drawing_area(GtkWidget *drawing_area,
                                                   DisplayState *ds);

#endif
