#ifndef PLUG_PROPTYPES_H
#define PLUG_PROPTYPES_H

#include "plug.h"

typedef bool (PlugPropertyGetterBool)(Plug *plug);
typedef void (PlugPropertySetterBool)(Plug *plug, bool value);

void plug_add_property_bool(Plug *plug, const char *name,
                            PlugPropertyGetterBool *getter,
                            PlugPropertySetterBool *setter,
                            int flags);

#endif
