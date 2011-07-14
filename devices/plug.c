#include "devices/plug.h"

G_DEFINE_TYPE(QemuPlug, qemu_plug, G_TYPE_OBJECT)

typedef struct QemuPlugProperty
{
    const gchar *name;
    const gchar *type;
    QemuPlugPropertyAccessor *getter;
    QemuPlugPropertyAccessor *setter;
    void *opaque;
    int flags;
} QemuPlugProperty;

void qemu_plug_install_property(QemuPlug *obj, const gchar *name, const gchar *type,
                                QemuPlugPropertyAccessor *getter,
                                QemuPlugPropertyAccessor *setter,
                                int flags, void *opaque)
{
    QemuPlugProperty *prop = qemu_malloc(sizeof(*prop));

    prop->name = name;
    prop->type = type;
    prop->getter = getter;
    prop->setter = setter;
    prop->opaque = opaque;
    prop->flags = flags;

    obj->props = g_slist_prepend(obj->props, prop);
}

static QemuPlugProperty *qemu_plug_find_property(QemuPlug *plug, const gchar *name)
{
    GSList *i;

    for (i = plug->props; i; i = i->next) {
        QemuPlugProperty *prop = i->data;

        if (strcmp(prop->name, name) == 0) {
            return prop;
        }
    }

    return NULL;
}

void qemu_plug_get_property(QemuPlug *plug, const gchar *name, Visitor *v, Error **errp)
{
    QemuPlugProperty *prop = qemu_plug_find_property(plug, name);

    g_assert(prop != NULL);
    prop->getter(plug, name, v, errp);
}

void qemu_plug_set_property(QemuPlug *plug, const gchar *name, Visitor *v, Error **errp)
{
    QemuPlugProperty *prop = qemu_plug_find_property(plug, name);

    g_assert(prop != NULL);
    prop->setter(plug, name, v, errp);
}

void qemu_plug_foreach_property(QemuPlug *plug, QemuPlugPropertyEnumerator *enumfn, void *opaque)
{
    GSList *i;

    for (i = plug->props; i; i = i->next) {
        QemuPlugProperty *prop = i->data;

        enumfn(plug, prop->name, prop->type, prop->flags, opaque);
    }
}

static void qemu_plug_init(QemuPlug *plug)
{
}

static void qemu_plug_finalize(QemuPlug *plug)
{
    while (plug->props) {
        qemu_free(plug->props->data);
        plug->props = g_slist_delete_link(plug->props, plug->props);
    }

    G_OBJECT_CLASS(qemu_plug_parent_class)->finalize(G_OBJECT(plug));
}

static void qemu_plug_class_init(QemuPlugClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = (GObjectFinalizeFunc)qemu_plug_finalize;
}

