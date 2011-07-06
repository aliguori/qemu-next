#ifndef VARIANT_H
#define VARIANT_H

#include <stdint.h>
#include <stdbool.h>

#define strify_i(a) # a
#define strify(a) strify_i(a)

#define typeis(lhs, rhs) \
    __builtin_types_compatible_p(lhs, rhs)

#define typename_check(lhs, rhs, name)  \
    if (typeis(lhs, rhs)) {             \
        v = strify(rhs);                \
    } else

typedef void (GenericEtter)(void);

typedef void (EtterTrampoline)(Plug *, const char *, GenericEtter *, Visitor *, Error **);


#include "variants-gen.h"

#define setter_compatible(type, fn) \
    __builtin_types_compatible_p(typeof(fn), void (*)(Plug *, type))

#define getter_compatible(type, fn) \
    __builtin_types_compatible_p(typeof(fn), type (*)(Plug *))

#define BUILD_ASSERT(cond) do { (void)sizeof(int[!!(cond)-1]); } while (0)

#define plug_add_property(plug, name, type, getter, setter) do { \
    BUILD_ASSERT(getter_compatible(type, getter)); \
    BUILD_ASSERT(setter_compatible(type, setter)); \
    plug_add_property_full(plug, name, typename(type), plug_getter_lookup(typename(type)), plug_setter_lookup(typename(type))); \
    } while (0)

#endif
