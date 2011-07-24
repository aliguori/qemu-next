#include "qemu/plug.h"
#include "qerror.h"
#include "qom/string-visitor.h"

#define MAX_NAME (128 + 1)
#define MAX_TYPENAME (32 + 1)

struct PlugProperty
{
    char name[MAX_NAME];
    char typename[MAX_TYPENAME];
    
    PlugPropertyAccessor *getter;
    PlugPropertyAccessor *setter;
    PlugPropertyFinalize *finalize;
    void *opaque;

    int flags;

    PlugProperty *next;
};

void plug_add_property_full(Plug *plug, const char *name,
                            PlugPropertyAccessor *getter,
                            PlugPropertyAccessor *setter,
                            PlugPropertyFinalize *fini,
                            void *opaque, const char *typename, int flags)
{
    PlugProperty *prop = qemu_mallocz(sizeof(*prop));

    snprintf(prop->name, sizeof(prop->name), "%s", name);
    snprintf(prop->typename, sizeof(prop->typename), "%s", typename);

    prop->getter = getter;
    prop->setter = setter;
    prop->finalize = fini;
    prop->opaque = opaque;

    prop->flags = flags;

    prop->next = plug->first_prop;
    plug->first_prop = prop;
}

static PlugProperty *plug_find_property(Plug *plug, const char *name)
{
    PlugProperty *prop;

    for (prop = plug->first_prop; prop; prop = prop->next) {
        if (strcmp(prop->name, name) == 0) {
            return prop;
        }
    }

    return NULL;
}

void plug_set_property(Plug *plug, const char *name, Visitor *v, Error **errp)
{
    PlugProperty *prop = plug_find_property(plug, name);

    if (!prop) {
        error_set(errp, QERR_PROPERTY_NOT_FOUND,
                  type_get_id(TYPE_INSTANCE(plug)), name);
        return;
    }

    if (!(prop->flags & PROP_F_WRITE) || (prop->flags & PROP_F_LOCKED)) {
        error_set(errp, QERR_PROPERTY_READ_ONLY,
                  type_get_id(TYPE_INSTANCE(plug)), name);
        return;
    }

    prop->flags |= PROP_F_DIRTY;

    prop->setter(plug, name, v, prop->opaque, errp);
}

void plug_get_property(Plug *plug, const char *name, Visitor *v, Error **errp)
{
    PlugProperty *prop = plug_find_property(plug, name);

    if (!prop) {
        error_set(errp, QERR_PROPERTY_NOT_FOUND,
                  type_get_id(TYPE_INSTANCE(plug)), name);
        return;
    }

    if (!(prop->flags & PROP_F_READ)) {
        error_set(errp, QERR_PROPERTY_READ_ONLY,
                  type_get_id(TYPE_INSTANCE(plug)), name);
        return;
    }

    prop->getter(plug, name, v, prop->opaque, errp);
}

void plug_lock_property(Plug *plug, const char *name)
{
    PlugProperty *prop = plug_find_property(plug, name);

    assert(prop != NULL);

    prop->flags |= PROP_F_LOCKED;
}

void plug_unlock_property(Plug *plug, const char *name)
{
    PlugProperty *prop = plug_find_property(plug, name);

    assert(prop != NULL);

    prop->flags &= ~PROP_F_LOCKED;
}

void plug_lock_all_properties(Plug *plug)
{
    PlugProperty *prop;

    for (prop = plug->first_prop; prop; prop = prop->next) {
        prop->flags |= PROP_F_LOCKED;
    }
}

void plug_unlock_all_properties(Plug *plug)
{
    PlugProperty *prop;

    for (prop = plug->first_prop; prop; prop = prop->next) {
        prop->flags &= ~PROP_F_LOCKED;
    }
}

void plug_foreach_property(Plug *plug, PropertyEnumerator *enumfn, void *opaque)
{
    PlugProperty *prop;

    for (prop = plug->first_prop; prop; prop = prop->next) {
        enumfn(plug, prop->name, prop->typename, prop->flags, opaque);
    }
}

void plug_set_realized(Plug *plug, bool realized)
{
    PlugClass *class = PLUG_GET_CLASS(plug);
    bool old_value = plug->realized;

    plug->realized = realized;

    if (plug->realized && !old_value) {
        if (class->realize) {
            class->realize(plug);
        }
    } else if (!plug->realized && old_value) {
        if (class->unrealize) {
            class->unrealize(plug);
        }
    }
}

bool plug_get_realized(Plug *plug)
{
    return plug->realized;
}

void plug_realize_all(Plug *plug)
{
    /* This doesn't loop infinitely because the callbacks are only called when
     * the state changes. */
    plug_set_realized(plug, true);
    plug_lock_all_properties(plug);
}

void plug_unrealize_all(Plug *plug)
{
    /* This doesn't loop infinitely because the callbacks are only called when
     * the state changes. */
    plug_set_realized(plug, false);
    plug_unlock_all_properties(plug);
}

static void plug_class_initfn(TypeClass *base_class)
{
    PlugClass *class = PLUG_CLASS(base_class);

    class->realize = plug_realize_all;
    class->unrealize = plug_unrealize_all;
}

static void plug_initfn(TypeInstance *inst)
{
    Plug *obj = PLUG(inst);

    plug_add_property_bool(obj, "realized",
                           plug_get_realized,
                           plug_set_realized,
                           PROP_F_READWRITE);
}

static void plug_finifn(TypeInstance *inst)
{
    Plug *plug = PLUG(inst);

    while (plug->first_prop) {
        PlugProperty *p = plug->first_prop;

        plug->first_prop = plug->first_prop->next;
        if (p->finalize) {
            p->finalize(plug, p->name, p->opaque);
        }
        qemu_free(p);
    }
}

static const TypeInfo plug_type_info = {
    .name = TYPE_PLUG,
    .instance_size = sizeof(Plug),
    .class_size = sizeof(PlugClass),
    .instance_init = plug_initfn,
    .instance_finalize = plug_finifn,
    .class_init = plug_class_initfn,
};

static void register_devices(void)
{
    type_register_static(&plug_type_info);
}

device_init(register_devices);
