/** FIXME: move to generated code **/

#include "qemu/plug-proptypes.h"

typedef struct FunctionPointer
{
    void (*fn)(void);
} FunctionPointer;

#define GEN_PROPDEF(ctype, typename, ctypename) \
static void CONCAT(plug_get_property__, ctypename)(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp) \
{ \
    FunctionPointer *fp = opaque; \
    CONCAT(PlugPropertyGetter, typename) *getter = (CONCAT(PlugPropertyGetter, typename) *)fp->fn; \
    int64_t value; \
 \
    value = getter(plug); \
    visit_type_int(v, &value, name, errp); \
} \
 \
static void CONCAT(plug_set_property__, ctypename)(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp) 
{ \
    FunctionPointer *fp = opaque; \
    CONCAT(PlugPropertySetter, ctypename) *setter = (CONCAT(PlugPropertySetter, ctypename) *)fp->fn; \
    int64_t value = 0; \
    Error *local_err = NULL; \
 \
    visit_type_int(v, &value, name, &local_err); \
    if (local_err) { \
        error_propagate(errp, local_err); \
        return; \
    } \
 \
    setter(plug, value); \
} \
 \
void CONCAT(plug_add_property_, ctypename)(Plug *plug, const char *name, \
                                           CONCAT(PlugPropertyGetter, ctypename) *getter, \
                                           CONCAT(PlugPropertySetter, ctypename) *setter, \
                                           int flags) \
{ \
    FunctionPointer *getter_fp = qemu_mallocz(sizeof(*getter_fp)); \
    FunctionPointer *setter_fp = qemu_mallocz(sizeof(*setter_fp)); \
 \
    getter_fp->fn = (void (*)(void))getter; \
    setter_fp->fn = (void (*)(void))setter; \
 \
    plug_add_property_full(plug, name, \
                           CONCAT(plug_get_property__, ctypename), getter_fp, \
                           CONCAT(plug_set_property__, ctypename), setter_fp, \
                           STRIFY(ctypename), flags); \
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

#undefine GEN_PROP

void plug_add_property_bool(Plug *plug, const char *name,
                            PlugPropertyGetterBool *getter,
                            PlugPropertySetterBool *setter,
                            int flags)
{
    FunctionPointer *getter_fp = qemu_mallocz(sizeof(*getter_fp));
    FunctionPointer *setter_fp = qemu_mallocz(sizeof(*setter_fp));

    getter_fp->fn = (void (*)(void))getter;
    setter_fp->fn = (void (*)(void))setter;

    plug_add_property_full(plug, name,
                           plug_get_property__bool, getter_fp,
                           plug_set_property__bool, setter_fp,
                           "bool", flags);
}

/** str **/

static void plug_get_property__str(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    FunctionPointer *fp = opaque;
    PlugPropertyGetterStr *getter = (PlugPropertyGetterStr *)fp->fn;
    char *value;

    value = (char *)getter(plug);
    if (value == NULL) {
        value = (char *)"";
    }
    visit_type_str(v, &value, name, errp);
}

static void plug_set_property__str(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    FunctionPointer *fp = opaque;
    PlugPropertySetterStr *setter = (PlugPropertySetterStr *)fp->fn;
    char *value = false;
    Error *local_err = NULL;

    visit_type_str(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    setter(plug, value);

    qemu_free(value);
}

void plug_add_property_str(Plug *plug, const char *name,
                           PlugPropertyGetterStr *getter,
                           PlugPropertySetterStr *setter,
                           int flags)
{
    FunctionPointer *getter_fp = qemu_mallocz(sizeof(*getter_fp));
    FunctionPointer *setter_fp = qemu_mallocz(sizeof(*setter_fp));

    getter_fp->fn = (void (*)(void))getter;
    setter_fp->fn = (void (*)(void))setter;

    plug_add_property_full(plug, name,
                           plug_get_property__str, getter_fp,
                           plug_set_property__str, setter_fp,
                           "str", flags);
}

typedef struct PlugData
{
    const char *typename;
    Plug *value;
} PlugData;

static void plug_get_property__plug(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    PlugData *data = opaque;
    char *value;

    value = (char *)TYPE_INSTANCE(data->value)->id;
    printf("plug -- %s\n", value);
    printf("visitor - %p\n", v->type_str);
    visit_type_str(v, &value, name, errp);
}

void plug_add_property_plug(Plug *plug, const char *name, Plug *value, const char *typename)
{
    PlugData *data = qemu_mallocz(sizeof(*data));
    char fulltype[33];

    data->typename = typename;
    data->value = value;

    snprintf(fulltype, sizeof(fulltype), "plug<%s>", typename);

    plug_add_property_full(plug, name,
                           plug_get_property__plug, data,
                           NULL, NULL,
                           fulltype, PROP_F_READ);
}

typedef struct SocketData
{
    const char *typename;
    Plug **value;
} SocketData;

static void plug_get_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    SocketData *data = opaque;
    const char *value = "";

    if (*data->value) {
        value = TYPE_INSTANCE(*data->value)->id;
    }

    visit_type_str(v, (char **)&value, name, errp);
}

static void plug_set_property__socket(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    char *value = NULL;
    Error *local_err = NULL;
    SocketData *data = opaque;
    TypeInstance *obj;

    /* FIXME: memleak? */
    visit_type_str(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    obj = type_find_by_id(value);
    assert(obj != NULL);

    *data->value = PLUG(type_dynamic_cast_assert(obj, data->typename));
}

void plug_add_property_socket(Plug *plug, const char *name, Plug **value, const char *typename)
{
    SocketData *data = qemu_mallocz(sizeof(*data));
    char fulltype[33];

    data->typename = typename;
    data->value = value;

    snprintf(fulltype, sizeof(fulltype), "socket<%s>", typename);

    plug_add_property_full(plug, name,
                           plug_get_property__socket, data,
                           plug_set_property__socket, data,
                           fulltype, PROP_F_READWRITE);
}

