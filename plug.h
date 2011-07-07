#ifndef PLUG_H
#define PLUG_H

#include "type.h"
#include "error.h"
#include "qapi/qapi-visit-core.h"

typedef struct Plug Plug;
typedef struct PlugProperty PlugProperty;

typedef void (PlugPropertyAccessor)(Plug *, const char *, Visitor *, void *, Error **errp);

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

    PlugProperty *next;
};

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

void plug_initialize(Plug *plug, const char *id);
void plug_finalize(Plug *plug);

void plug_set_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_get_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_add_property_full(Plug *plug, const char *name,
                            PlugPropertyAccessor *getter, void *getter_opaque,
                            PlugPropertyAccessor *setter, void *setter_opaque,
                            const char *typename);

#include "plug-proptypes.h"

#endif
