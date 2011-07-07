/** FIXME: move to generated code **/

#include "plug-proptypes.h"

typedef struct FunctionPointer
{
    void (*fn)(void);
} FunctionPointer;

void plug_get_property__int(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    FunctionPointer *fp = opaque;
    int64_t (*getter)(Plug *) = (int64_t (*)(Plug *))fp->fn;
    int64_t value;

    value = getter(plug);
    visit_type_int(v, &value, name, errp);
}

void plug_set_property__int(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    FunctionPointer *fp = opaque;
    void (*setter)(Plug *, int64_t) = (void (*)(Plug *, int64_t))fp->fn;
    int64_t value = 0;
    Error *local_err = NULL;

    visit_type_int(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    setter(plug, value);
}

void plug_add_property_int(Plug *plug, const char *name,
                           int64_t (*getter)(Plug *plug),
                           void (*setter)(Plug *plug, int64_t))
{
    FunctionPointer *getter_fp = qemu_mallocz(sizeof(*getter_fp));
    FunctionPointer *setter_fp = qemu_mallocz(sizeof(*setter_fp));

    getter_fp->fn = (void (*)(void))getter;
    setter_fp->fn = (void (*)(void))setter;

    plug_add_property_full(plug, name,
                           plug_get_property__int, getter_fp,
                           plug_set_property__int, setter_fp,
                           "int");
}

typedef struct PlugData
{
    const char *typename;
    Plug *value;
} PlugData;

void plug_get_property__plug(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    PlugData *data = opaque;
    char *value;

    value = (char *)TYPE_INSTANCE(data->value)->id;
    visit_type_str(v, &value, name, errp);
}

void plug_add_property_plug(Plug *plug, const char *name, Plug *value, const char *typename)
{
    PlugData *data = qemu_mallocz(sizeof(*data));
    char fulltype[33];

    data->typename = typename;
    data->value = value;

    snprintf(fulltype, sizeof(fulltype), "plug<%s>", typename);

    plug_add_property_full(plug, name,
                           plug_get_property__plug, data,
                           NULL, NULL,
                           fulltype);
}

typedef struct SocketData
{
    const char *typename;
    Plug **value;
} SocketData;

void plug_get_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    SocketData *data = opaque;
    const char *value = "";

    if (*data->value) {
        value = TYPE_INSTANCE(*data->value)->id;
    }

    visit_type_str(v, (char **)&value, name, errp);
}

void plug_set_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    char *value = NULL;
    Error *local_err = NULL;
    SocketData *data = opaque;
    TypeInstance *obj;

    /* FIXME: memleak? */
    visit_type_str(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    obj = type_find_by_id(value);
    assert(obj != NULL);

    *data->value = PLUG(type_check_type(obj, data->typename));
}

void plug_add_property_socket(Plug *plug, const char *name, Plug **value, const char *typename)
{
    SocketData *data = qemu_mallocz(sizeof(*data));
    char fulltype[33];

    data->typename = typename;
    data->value = value;

    snprintf(fulltype, sizeof(fulltype), "socket<%s>", typename);

    plug_add_property_full(plug, name,
                           plug_get_property__socket, data,
                           plug_set_property__socket, data,
                           fulltype);
}

