#ifndef QEMU_GTK_DISPLAY_H
#define QEMU_GTK_DISPLAY_H

#include <gtk/gtkdrawingarea.h>

typedef struct _QemuDisplay QemuDisplay;
typedef struct _QemuDisplayClass QemuDisplayClass;
typedef struct _QemuDisplayPrivate QemuDisplayPrivate;

#define QEMU_TYPE_DISPLAY (qemu_display_get_type())

#define QEMU_DISPLAY(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj), QEMU_TYPE_DISPLAY, QemuDisplay))

#define QEMU_DISPLAY_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass), QEMU_TYPE_DISPLAY, QemuDisplayClass))

#define QEMU_IS_DISPLAY(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj), QEMU_TYPE_DISPLAY))

#define QEMU_IS_DISPLAY_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass), QEMU_TYPE_DISPLAY))

#define QEMU_DISPLAY_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj), QEMU_TYPE_DISPLAY, QemuDisplayClass))

struct _QemuDisplay
{
    GtkDrawingArea parent;
    QemuDisplayPrivate *priv;
};

struct _QemuDisplayClass
{
    GtkDrawingAreaClass parent_class;
};

GType qemu_display_get_type(void);
GtkWidget *qemu_display_new(void);

void qemu_display_update(QemuDisplay *obj, DisplayState *ds,
                         gint x, gint y, gint w, gint h);

void qemu_display_resize(QemuDisplay *obj, DisplayState *ds);

void qemu_display_set_grab(QemuDisplay *obj, gboolean enable);
void qemu_display_set_host_key(QemuDisplay *obj, gint num_keys,
                               const gint *keys);
void qemu_display_set_click_to_grab(QemuDisplay *obj, gboolean enable);

gboolean qemu_display_get_grab(QemuDisplay *obj);
gint qemu_display_get_host_keys(QemuDisplay *obj, gint num_keys, gint *keys);
gboolean qemu_display_get_click_to_grab(QemuDisplay *obj);
gboolean qemu_display_get_relative_pointer(QemuDisplay *obj);

#endif
