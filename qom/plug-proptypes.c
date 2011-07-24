/** FIXME: move to generated code **/

#include "qemu/plug-proptypes.h"

typedef struct FunctionPointer
{
    void (*getter)(void);
    void (*setter)(void);
} FunctionPointer;

static void plug_get_property__bool(Plug *plug, const char *name, Visitor *v, void *opaque, Error **errp)
{
    FunctionPointer *fp = opaque;
    PlugPropertyGetterBool *getter = (PlugPropertyGetterBool *)fp->getter;
    bool value;

    value = getter(plug);
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

    setter(plug, value);
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
