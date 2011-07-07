/** FIXME: move to generated code **/

#ifndef PLUG_PROPTYPES_H
#define PLUG_PROPTYPES_H

#include "plug.h"

void plug_get_property__int(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
void plug_set_property__int(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
void plug_add_property_int(Plug *plug, const char *name,
                           int64_t (*getter)(Plug *plug),
                           void (*setter)(Plug *plug, int64_t));

void plug_get_property__bool(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
void plug_set_property__bool(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
void plug_add_property_bool(Plug *plug, const char *name,
                            bool (*getter)(Plug *plug),
                            void (*setter)(Plug *plug, bool));

void plug_get_property__plug(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);

void plug_add_property_plug(Plug *plug, const char *name, Plug *value, const char *typename);

void plug_get_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);
void plug_set_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp);

void plug_add_property_socket(Plug *plug, const char *name, Plug **value, const char *typename);


#endif
