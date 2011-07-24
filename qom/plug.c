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
        error_set(errp, QERR_PROPERTY_NOT_FOUND, type_get_id(TYPE_INSTANCE(plug)), name);
        return;
    }

    if (!(prop->flags & PROP_F_WRITE) || (prop->flags & PROP_F_LOCKED)) {
        error_set(errp, QERR_PROPERTY_READ_ONLY, type_get_id(TYPE_INSTANCE(plug)), name);
        return;
    }

    prop->setter(plug, name, v, prop->opaque, errp);
}

void plug_get_property(Plug *plug, const char *name, Visitor *v, Error **errp)
{
    PlugProperty *prop = plug_find_property(plug, name);

    if (!prop) {
        error_set(errp, QERR_PROPERTY_NOT_FOUND, type_get_id(TYPE_INSTANCE(plug)), name);
        printf("property not found\n");
        return;
    }

    if (!(prop->flags & PROP_F_READ)) {
        error_set(errp, QERR_PROPERTY_READ_ONLY, type_get_id(TYPE_INSTANCE(plug)), name);
        printf("property read only\n");
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

static char *plug_get_property_str(Plug *plug, const char *name, Error **errp)
{
    StringOutputVisitor sov;

    string_output_visitor_init(&sov);
    plug_get_property(plug, name, &sov.parent, errp);

    return qemu_strdup(sov.value);
}

static void plug_propagate_realized(Plug *plug, const char *name,
                                    const char *typename, int flags,
                                    void *opaque)
{
    if (strstart(typename, "plug<", NULL)) {
        char *child_name;
        Plug *child_plug;

        child_name = plug_get_property_str(plug, name, NULL);
        child_plug = PLUG(type_find_by_id(child_name));

        plug_set_realized(child_plug, plug_get_realized(plug));

        qemu_free(child_name);
    }
}

typedef struct PlugData
{
    const char *typename;
    Plug *value;
} PlugData;

static void plug_get_property__plug(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    PlugData *data = opaque;
    char *value;

    value = (char *)TYPE_INSTANCE(data->value)->id;
    visit_type_str(v, &value, name, errp);
}

static void plug_del_property__plug(Plug *plug, const char *name, void *opaque)
{
    PlugData *data = opaque;

    type_finalize(data->value);
    qemu_free(data);
}

void plug_add_property_plug(Plug *plug, Plug *value, const char *typename,
                            const char *name, ...)
{
    PlugData *data = qemu_mallocz(sizeof(*data));
    char fullid[MAX_NAME];
    char fulltype[MAX_TYPENAME];
    size_t off;
    va_list ap;

    data->typename = typename;
    data->value = value;

    snprintf(fulltype, sizeof(fulltype), "plug<%s>", typename);

    va_start(ap, name);
    off = snprintf(fullid, sizeof(fullid), "%s::",
                   type_get_id(TYPE_INSTANCE(plug)));
    vsnprintf(&fullid[off], sizeof(fullid) - off, name, ap);
    va_end(ap);

    type_initialize(plug, typename, fullid);

    plug_add_property_full(plug, name,
                           plug_get_property__plug,
                           NULL,
                           plug_del_property__plug,
                           data, fulltype, PROP_F_READ);
}

Plug *plug_get_property_plug(Plug *plug, Error **errp, const char *name, ...)
{
    char fullname[MAX_NAME];
    char *plugname;
    Plug *value;
    va_list ap;

    va_start(ap, name);
    vsnprintf(fullname, sizeof(fullname), name, ap);
    va_end(ap);

    plugname = plug_get_property_str(plug, fullname, errp);
    if (error_is_set(errp)) {
        return NULL;
    }

    value = PLUG(type_find_by_id(plugname));

    qemu_free(plugname);

    return value;
}

typedef struct SocketData
{
    const char *typename;
    Plug **value;
} SocketData;

static void plug_get_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    SocketData *data = opaque;
    const char *value = "";

    if (*data->value) {
        value = TYPE_INSTANCE(*data->value)->id;
    }

    visit_type_str(v, (char **)&value, name, errp);
}

static void plug_set_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    char *value = NULL;
    Error *local_err = NULL;
    SocketData *data = opaque;
    TypeInstance *obj;

    visit_type_str(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    obj = type_find_by_id(value);
    assert(obj != NULL);

    *data->value = PLUG(type_dynamic_cast_assert(obj, data->typename));

    qemu_free(value);
}

static void plug_del_property__socket(Plug *plug, const char *name, void *opaque)
{
    SocketData *data = opaque;

    qemu_free(data);
}

void plug_add_property_socket(Plug *plug, const char *name, Plug **value, const char *typename)
{
    SocketData *data = qemu_mallocz(sizeof(*data));
    char fulltype[33];

    data->typename = typename;
    data->value = value;

    snprintf(fulltype, sizeof(fulltype), "socket<%s>", typename);

    plug_add_property_full(plug, name,
                           plug_get_property__socket,
                           plug_set_property__socket,
                           plug_del_property__socket,
                           data,
                           fulltype, PROP_F_READWRITE);
}

void plug_realize_all(Plug *plug)
{
    /* This doesn't loop infinitely because the callbacks are only called when
     * the state changes. */
    plug_set_realized(plug, true);
    plug_lock_all_properties(plug);
    plug_foreach_property(plug, plug_propagate_realized, NULL);
}

void plug_unrealize_all(Plug *plug)
{
    /* This doesn't loop infinitely because the callbacks are only called when
     * the state changes. */
    plug_set_realized(plug, false);
    plug_unlock_all_properties(plug);
    plug_foreach_property(plug, plug_propagate_realized, NULL);
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
