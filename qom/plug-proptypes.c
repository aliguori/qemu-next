/** FIXME: move to generated code **/

#include "qemu/plug-proptypes.h"

typedef struct FunctionPointer
{
    void (*getter)(void);
    void (*setter)(void);
} FunctionPointer;

#define STRIFY_I(a) # a
#define STRIFY(a) STRIFY_I(a)

static void plug_del_property__fp(Plug *plug, const char *name, void *opaque)
{
    FunctionPointer *fp = opaque;
    qemu_free(fp);
}

#define GEN_PROP(ctype, typename, ctypename) \
static void CONCAT(plug_get_property__, ctypename)(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp) \
{ \
    FunctionPointer *fp = opaque; \
    CONCAT(PlugPropertyGetter, typename) *getter = (CONCAT(PlugPropertyGetter, typename) *)fp->getter; \
    int64_t value; \
 \
    value = getter(plug, errp);                 \
    visit_type_int(v, &value, name, errp); \
} \
 \
static void CONCAT(plug_set_property__, ctypename)(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)  \
{ \
    FunctionPointer *fp = opaque; \
    CONCAT(PlugPropertySetter, typename) *setter = (CONCAT(PlugPropertySetter, typename) *)fp->setter; \
    int64_t value = 0; \
    Error *local_err = NULL; \
 \
    visit_type_int(v, &value, name, &local_err); \
    if (local_err) { \
        error_propagate(errp, local_err); \
        return; \
    } \
 \
    setter(plug, value, errp);                       \
} \
 \
void CONCAT(plug_add_property_, ctypename)(Plug *plug, const char *name, \
                                           CONCAT(PlugPropertyGetter, typename) *getter, \
                                           CONCAT(PlugPropertySetter, typename) *setter, \
                                           int flags) \
{ \
    FunctionPointer *fp = qemu_mallocz(sizeof(*fp)); \
 \
    fp->getter = (void (*)(void))getter; \
    fp->setter = (void (*)(void))setter; \
 \
    plug_add_property_full(plug, name, \
                           CONCAT(plug_get_property__, ctypename), \
                           CONCAT(plug_set_property__, ctypename), \
                           plug_del_property__fp, \
                           fp, STRIFY(ctypename), flags); \
}

GEN_PROP(int8_t, Int8, int8);
GEN_PROP(int16_t, Int16, int16);
GEN_PROP(int32_t, Int32, int32);
GEN_PROP(int64_t, Int64, int64);
GEN_PROP(uint8_t, UInt8, uint8);
GEN_PROP(uint16_t, UInt16, uint16);
GEN_PROP(uint32_t, UInt32, uint32);
GEN_PROP(uint64_t, UInt64, uint64);
GEN_PROP(int64_t, Int, int);

#undef GEN_PROP

static void plug_get_property__bool(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    FunctionPointer *fp = opaque;
    PlugPropertyGetterBool *getter = (PlugPropertyGetterBool *)fp->getter;
    bool value;

    value = getter(plug, errp);
    visit_type_bool(v, &value, name, errp);
}

static void plug_set_property__bool(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    FunctionPointer *fp = opaque;
    PlugPropertySetterBool *setter = (PlugPropertySetterBool *)fp->setter;
    bool value = false;
    Error *local_err = NULL;

    visit_type_bool(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    setter(plug, value, errp);
}

void plug_add_property_bool(Plug *plug, const char *name,
                            PlugPropertyGetterBool *getter,
                            PlugPropertySetterBool *setter,
                            int flags)
{
    FunctionPointer *fp = qemu_mallocz(sizeof(*fp));

    fp->getter = (void (*)(void))getter;
    fp->setter = (void (*)(void))setter;

    plug_add_property_full(plug, name,
                           plug_get_property__bool,
                           plug_set_property__bool,
                           plug_del_property__fp,
                           fp, "bool", flags);
}

/** str **/

static void plug_get_property__str(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    FunctionPointer *fp = opaque;
    PlugPropertyGetterStr *getter = (PlugPropertyGetterStr *)fp->getter;
    char *value;

    value = (char *)getter(plug, errp);
    if (value == NULL) {
        value = (char *)"";
    }
    visit_type_str(v, &value, name, errp);
}

static void plug_set_property__str(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    FunctionPointer *fp = opaque;
    PlugPropertySetterStr *setter = (PlugPropertySetterStr *)fp->setter;
    char *value = false;
    Error *local_err = NULL;

    visit_type_str(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    setter(plug, value, errp);

    qemu_free(value);
}

void plug_add_property_str(Plug *plug, const char *name,
                           PlugPropertyGetterStr *getter,
                           PlugPropertySetterStr *setter,
                           int flags)
{
    FunctionPointer *fp = qemu_mallocz(sizeof(*fp));

    fp->getter = (void (*)(void))getter;
    fp->setter = (void (*)(void))setter;

    plug_add_property_full(plug, name,
                           plug_get_property__str,
                           plug_set_property__str,
                           plug_del_property__fp,
                           fp, "str", flags);
}
