#ifndef QEMU_TYPE_H
#define QEMU_TYPE_H

#include "qemu-common.h"
#include <glib.h>

#define MAX_ID (32 + 1)

typedef uint64_t Type;

typedef struct TypeClass TypeClass;
typedef struct TypeInstance TypeInstance;
typedef struct TypeInfo TypeInfo;

typedef struct Interface Interface;
typedef struct InterfaceInfo InterfaceInfo;

struct TypeClass
{
    Type type;
};

struct TypeInstance
{
    TypeClass *class;

    char id[MAX_ID];

    GSList *interfaces;
};

struct TypeInfo
{
    /* Name of type */
    const char *name;

    /* Name of parent type */
    const char *parent;

    /* Size of the class */
    size_t class_size;

    /* Size of an instance */
    size_t instance_size;

    /* Used during class initialization.
     *
     * base_init is called after memcpy()'ing the base class into the new
     * class.  The purpose is to allow reinitialization of any dynamically
     * allocated class members.  If you just have non-dynamic class members
     * (such as function pointers), you don't need to implement this function.
     *
     * class_init is called after initializing all of the base classes.  This
     * is where you should set all class members (dynamic or static).
     */
    void (*base_init)(TypeClass *klass);
    void (*base_finalize)(TypeClass *klass);

    void (*class_init)(TypeClass *klass);
    void (*class_finalize)(TypeClass *klass);

    void (*instance_init)(TypeInstance *obj);
    void (*instance_finalize)(TypeInstance *obj);

    InterfaceInfo *interfaces;
};

#define TYPE_INSTANCE(obj) ((TypeInstance *)(obj))
#define TYPE_CHECK(type, obj, name) ((type *)type_dynamic_cast_assert((TypeInstance *)(obj), (name)))
#define TYPE_CLASS_CHECK(class, obj, name) ((class *)type_check_class((TypeClass *)(obj), (name)))
#define TYPE_GET_CLASS(class, obj, name) TYPE_CLASS_CHECK(class, type_get_class(TYPE_INSTANCE(obj)), name)

struct Interface
{
    TypeInstance parent;
    TypeInstance *obj;
};

struct InterfaceInfo
{
    const char *type;
    void (*interface_initfn)(TypeClass *class);
};

#define TYPE_INTERFACE "interface"
#define INTERFACE(obj) TYPE_CHECK(Interface, obj, TYPE_INTERFACE)

TypeInstance *type_new(const char *typename, const char *id);

void type_delete(TypeInstance *obj);

void type_initialize(void *obj, const char *typename, const char *id);

void type_finalize(void *obj);

TypeInstance *type_dynamic_cast(TypeInstance *obj, const char *typename);

TypeInstance *type_dynamic_cast_assert(TypeInstance *obj, const char *typename);

bool type_is_type(TypeInstance *obj, const char *typename);

TypeClass *type_get_class(TypeInstance *obj);

const char *type_get_id(TypeInstance *obj);

const char *type_get_type(TypeInstance *obj);

TypeClass *type_get_super(TypeInstance *obj);

/**/

Type type_register_static(const TypeInfo *info);

TypeInstance *type_find_by_id(const char *id);

TypeClass *type_check_class(TypeClass *obj, const char *typename);

Type type_get_by_name(const char *name);

const char *type_get_name(Type type);

void type_foreach(void (*enumfn)(TypeInstance *obj, void *opaque), void *opaque);

#endif
