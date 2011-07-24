#ifndef PLUG_PROPTYPES_H
#define PLUG_PROPTYPES_H

#include "plug.h"

#define CONCAT_I(a, b) a ## b
#define CONCAT(a, b) CONCAT_I(a, b)

#define GEN_PROP(ctype, typename, ctypename)                   \
typedef ctype (CONCAT(PlugPropertyGetter, typename))(Plug *plug, Error **errp);  \
typedef void (CONCAT(PlugPropertySetter, typename))(Plug *plug, ctype value, Error **errp); \
 \
void CONCAT(plug_add_property_, ctypename)(Plug *plug, const char *name, \
                                           CONCAT(PlugPropertyGetter, typename) *getter, \
                                           CONCAT(PlugPropertySetter, typename) *setter, \
                                           int flags)

GEN_PROP(int8_t, Int8, int8);
GEN_PROP(int16_t, Int16, int16);
GEN_PROP(int32_t, Int32, int32);
GEN_PROP(int64_t, Int64, int64);
GEN_PROP(uint8_t, UInt8, uint8);
GEN_PROP(uint16_t, UInt16, uint16);
GEN_PROP(uint32_t, UInt32, uint32);
GEN_PROP(uint64_t, UInt64, uint64);
GEN_PROP(int64_t, Int, int);
GEN_PROP(const char *, Str, str);

#undef GEN_PROP

typedef bool (PlugPropertyGetterBool)(Plug *plug, Error **errp);
typedef void (PlugPropertySetterBool)(Plug *plug, bool value, Error **errp);

void plug_add_property_bool(Plug *plug, const char *name,
                            PlugPropertyGetterBool *getter,
                            PlugPropertySetterBool *setter,
                            int flags);

#endif
