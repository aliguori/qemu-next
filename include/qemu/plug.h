#ifndef PLUG_H
#define PLUG_H

#include "qemu/type.h"
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
    PROP_F_LOCKED = 4,
    PROP_F_DIRTY = 8,
} PlugPropertyFlags;

struct Plug
{
    TypeInstance parent;

    /* private */
    bool realized;
    PlugProperty *first_prop;
};

typedef struct PlugClass {
    TypeClass parent_class;

    /* protected */
    void (*realize)(Plug *plug);
    void (*unrealize)(Plug *plug);
} PlugClass;

#define TYPE_PLUG "plug"
#define PLUG(obj) TYPE_CHECK(Plug, obj, TYPE_PLUG)
#define PLUG_GET_CLASS(obj) TYPE_GET_CLASS(PlugClass, obj, TYPE_PLUG)
#define PLUG_CLASS(obj) TYPE_CLASS_CHECK(PlugClass, obj, TYPE_PLUG)

typedef void (PlugPropertyAccessor)(Plug *plug, const char *name, Visitor *v,
                                    void *opaque, Error **errp);
typedef void (PlugPropertyFinalize)(Plug *plug, const char *name, void *opaque);
typedef void (PropertyEnumerator)(Plug *plug, const char *name,
                                  const char *typename, int flags,
                                  void *opaque);

void plug_set_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_get_property(Plug *plug, const char *name, Visitor *v, Error **errp);

void plug_add_property_full(Plug *plug, const char *name,
                            PlugPropertyAccessor *getter,
                            PlugPropertyAccessor *setter,
                            PlugPropertyFinalize *fini,
                            void *opaque,
                            const char *typename, int flags);

void plug_foreach_property(Plug *plug, PropertyEnumerator *enumfn,
                           void *opaque);

void plug_lock_property(Plug *plug, const char *name);
void plug_unlock_property(Plug *plug, const char *name);

void plug_lock_all_properties(Plug *plug);
void plug_unlock_all_properties(Plug *plug);

void plug_set_realized(Plug *plug, bool realized);
bool plug_get_realized(Plug *plug);

void plug_realize_all(Plug *plug);
void plug_unrealize_all(Plug *plug);

void plug_add_property_plug(Plug *plug, Plug *value, const char *typename, const char *name, ...);

Plug *plug_get_property_plug(Plug *plug, Error **errp, const char *name, ...);

void plug_add_property_socket(Plug *plug, const char *name, Plug **value, const char *typename);

#include "qemu/plug-proptypes.h"

#endif
