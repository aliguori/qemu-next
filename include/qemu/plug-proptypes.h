/** FIXME: move to generated code **/

#ifndef PLUG_PROPTYPES_H
#define PLUG_PROPTYPES_H

#include "plug.h"

void plug_add_property_int(Plug *plug, const char *name,
                           int64_t (*getter)(Plug *plug),
                           void (*setter)(Plug *plug, int64_t),
                           int flags);

typedef bool (PlugPropertyGetterBool)(Plug *plug);
typedef void (PlugPropertySetterBool)(Plug *plug, bool value);

void plug_add_property_bool(Plug *plug, const char *name,
                            bool (*getter)(Plug *plug),
                            void (*setter)(Plug *plug, bool),
                            int flags);

typedef const char *(PlugPropertyGetterStr)(Plug *plug);
typedef void (PlugPropertySetterStr)(Plug *plug, const char *value);

void plug_add_property_str(Plug *plug, const char *name,
                           PlugPropertyGetterStr *getter,
                           PlugPropertySetterStr *setter,
                           int flags);

void plug_add_property_plug(Plug *plug, const char *name, Plug *value, const char *typename);

void plug_add_property_socket(Plug *plug, const char *name, Plug **value, const char *typename);


#endif
