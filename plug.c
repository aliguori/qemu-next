#include "plug.h"

#define MAX_NAME (32 + 1)
#define MAX_TYPENAME (32 + 1)

struct PlugProperty
{
    char name[MAX_NAME];
    char typename[MAX_TYPENAME];
    

    PlugPropertyAccessor *getter;
    void *getter_opaque;

    PlugPropertyAccessor *setter;
    void *setter_opaque;

    int flags;

    PlugProperty *next;
};

void plug_add_property_full(Plug *plug, const char *name,
                            PlugPropertyAccessor *getter, void *getter_opaque,
                            PlugPropertyAccessor *setter, void *setter_opaque,
                            const char *typename, int flags)
{
    PlugProperty *prop = qemu_mallocz(sizeof(*prop));

    snprintf(prop->name, sizeof(prop->name), "%s", name);
    snprintf(prop->typename, sizeof(prop->typename), "%s", typename);

    prop->getter = getter;
    prop->getter_opaque = getter_opaque;

    prop->setter = setter;
    prop->setter_opaque = setter_opaque;

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

    assert((prop->flags & PROP_F_WRITE));
    assert(!plug->props_masked || !(prop->flags & PROP_F_MASKABLE));

    prop->setter(plug, name, v, prop->setter_opaque, errp);
}

void plug_get_property(Plug *plug, const char *name, Visitor *v, Error **errp)
{
    PlugProperty *prop = plug_find_property(plug, name);

    assert((prop->flags & PROP_F_READ));
    prop->getter(plug, name, v, prop->getter_opaque, errp);
}

void plug_foreach_property(Plug *plug, PropertyEnumerator *enumfn, void *opaque)
{
    PlugProperty *prop;

    for (prop = plug->first_prop; prop; prop = prop->next) {
        enumfn(plug, prop->name, prop->typename, prop->flags, opaque);
    }
}

void plug_set_properties_masked(Plug *plug, bool masked)
{
    plug->props_masked = masked;
}

bool plug_get_properties_masked(Plug *plug)
{
    return plug->props_masked;
}

void plug_initialize(Plug *plug, const char *id)
{
    type_initialize(plug, TYPE_PLUG, id);
}

void plug_finalize(Plug *plug)
{
    type_finalize(plug);
}

const char *plug_get_id(Plug *plug)
{
    return TYPE_INSTANCE(plug)->id;
}

static const TypeInfo plug_type_info = {
    .name = TYPE_PLUG,
    .instance_size = sizeof(Plug),
};

static void register_devices(void)
{
    type_register_static(&plug_type_info);
}

device_init(register_devices);
