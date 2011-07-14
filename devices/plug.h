#ifndef QEMU_PLUG_H
#define QEMU_PLUG_H

#include <glib-object.h>
#include "qemu-common.h"
#include "qapi/qapi-visit-core.h"

G_BEGIN_DECLS

#define QEMU_TYPE_PLUG		    (qemu_plug_get_type())
#define QEMU_PLUG(obj)	            (G_TYPE_CHECK_INSTANCE_CAST((obj), QEMU_TYPE_PLUG, QemuPlug))
#define QEMU_DISPLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), QEMU_TYPE_PLUG, QemuPlugClass))
#define QEMU_DISPLAY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), QEMU_TYPE_PLUG, QemuPlugClass))

typedef struct QemuPlug QemuPlug;
typedef struct QemuPlugClass QemuPlugClass;

typedef void (QemuPlugPropertyAccessor)(QemuPlug *obj, const gchar *name, Visitor *v, void *opaque);
typedef void (QemuPlugPropertyEnumerator)(QemuPlug *obj, const gchar *name, const gchar *type, int flags, void *opaque);

struct QemuPlug
{
    GObject parent;

    GSList *props;
};

struct QemuPlugClass
{
    GObjectClass parent_class;
};

GType		qemu_plug_get_type(void);

void qemu_plug_install_property(QemuPlug *obj, const gchar *name, const gchar *type,
                                QemuPlugPropertyAccessor *getter,
                                QemuPlugPropertyAccessor *setter,
                                int flags, void *opaque);

void qemu_plug_get_property(QemuPlug *plug, const gchar *name, Visitor *v, Error **errp);
void qemu_plug_set_property(QemuPlug *plug, const gchar *name, Visitor *v, Error **errp);
void qemu_plug_foreach_property(QemuPlug *plug, QemuPlugPropertyEnumerator *enumfn, void *opaque);

G_END_DECLS

#endif
