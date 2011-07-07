#ifndef PLUG_H
#define PLUG_H

#include "type.h"
#include "error.h"
#include "qapi/qapi-visit-core.h"

typedef struct Plug Plug;
typedef struct PlugProperty PlugProperty;

typedef enum PlugPropertyFlags
{
    PROP_F_NONE = 0,
    PROP_F_READ = 1,
    PROP_F_WRITE = 2,
    PROP_F_READWRITE = (PROP_F_READ | PROP_F_WRITE),
    PROP_F_MASKABLE = 4,
} PlugPropertyFlags;

struct Plug
{
    TypeInstance parent;

    PlugProperty *first_prop;
    bool props_masked;
};

typedef struct PlugClass {
    TypeClass parent_class;
} PlugClass;

#define TYPE_PLUG "plug"
#define PLUG(obj) TYPE_CHECK(Plug, obj, TYPE_PLUG)

typedef void (PlugPropertyAccessor)(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
typedef void (PropertyEnumerator)(Plug *plug, const char *name, const char *typename, int flags, void *opaque);

void plug_initialize(Plug *plug, const char *id);
void plug_finalize(Plug *plug);

void plug_set_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_get_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_add_property_full(Plug *plug, const char *name,
                            PlugPropertyAccessor *getter, void *getter_opaque,
                            PlugPropertyAccessor *setter, void *setter_opaque,
                            const char *typename, int flags);

void plug_foreach_property(Plug *plug, PropertyEnumerator *enumfn, void *opaque);

void plug_set_properties_masked(Plug *plug, bool masked);

bool plug_get_properties_masked(Plug *plug);

const char *plug_get_id(Plug *plug);

#include "plug-proptypes.h"

#endif
