#ifndef PLUG_H
#define PLUG_H

#include "type.h"
#include "error.h"
#include "qapi/qapi-visit-core.h"

typedef struct Plug Plug;
typedef struct PlugProperty PlugProperty;

typedef void (PlugPropertyAccessor)(Plug *, const char *, Visitor *, void *, Error **errp);

struct PlugProperty
{
    const char *name;

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

void plug_set_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_get_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_add_property(Plug *plug, const char *name,
                       PlugPropertyAccessor *getter, void *getter_opaque,
                       PlugPropertyAccessor *setter, void *setter_opaque);

/** types **/

void plug_get_property__int(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
void plug_set_property__int(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
void plug_add_property_int(Plug *plug, const char *name,
                           int64_t (*getter)(Plug *plug),
                           void (*setter)(Plug *plug, int64_t));

void plug_get_property__plug(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);

void plug_add_property_plug(Plug *plug, const char *name, Plug *value, const char *typename);

void plug_get_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
void plug_set_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);

void plug_add_property_socket(Plug *plug, const char *name, Plug **value, const char *typename);

#endif
