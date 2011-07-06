#include "plug.h"

static void plug_initfn(TypeInstance *obj)
{
}

void plug_add_property(Plug *plug, const char *name,
                       PlugPropertyAccessor *getter, void *getter_opaque,
                       PlugPropertyAccessor *setter, void *setter_opaque)
{
    PlugProperty *prop = qemu_mallocz(sizeof(*prop));

    prop->name = name;

    prop->getter = getter;
    prop->getter_opaque = getter_opaque;

    prop->setter = setter;
    prop->setter_opaque = setter_opaque;

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
    prop->setter(plug, name, v, prop->setter_opaque, errp);
}

void plug_get_property(Plug *plug, const char *name, Visitor *v, Error **errp)
{
    PlugProperty *prop = plug_find_property(plug, name);
    prop->getter(plug, name, v, prop->getter_opaque, errp);
}

void plug_initialize(Plug *plug)
{
    type_initialize(plug, TYPE_PLUG);
}

static const TypeInfo plug_type_info = {
    .name = TYPE_PLUG,
    .instance_size = sizeof(Plug),
    .instance_init = plug_initfn,
};

static void register_devices(void)
{
    type_register_static(&plug_type_info);
}

device_init(register_devices);

/** FIXME: move to generated code **/

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

    plug_add_property(plug, name,
                      plug_get_property__int, getter_fp,
                      plug_set_property__int, setter_fp);
}
