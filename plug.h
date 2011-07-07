#ifndef PLUG_H
#define PLUG_H

#include "type.h"
#include "error.h"
#include "qapi/qapi-visit-core.h"

typedef struct Plug Plug;
typedef struct PlugProperty PlugProperty;

struct Plug
{
    TypeInstance parent;

    PlugProperty *first_prop;
};

typedef struct PlugClass {
    TypeClass parent_class;
} PlugClass;

#define TYPE_PLUG "plug"
#define PLUG(obj) TYPE_CHECK(Plug, obj, TYPE_PLUG)

typedef void (PlugPropertyAccessor)(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
typedef void (PropertyEnumerator)(Plug *plug, const char *name, const char *typename, void *opaque);

void plug_initialize(Plug *plug, const char *id);
void plug_finalize(Plug *plug);

void plug_set_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_get_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_add_property_full(Plug *plug, const char *name,
                            PlugPropertyAccessor *getter, void *getter_opaque,
                            PlugPropertyAccessor *setter, void *setter_opaque,
                            const char *typename);

void plug_foreach_property(Plug *plug, PropertyEnumerator *enumfn, void *opaque);

const char *plug_get_id(Plug *plug);

#include "plug-proptypes.h"

#endif
