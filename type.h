#ifndef QEMU_TYPE_H
#define QEMU_TYPE_H

#include "qemu-common.h"

/**
 * Generic QEMU Type infrastructure
 *
 * This is largely inspired by GType with a few notable differences:
 *
 * 1) Better support for non-heap objects
 * 2) Classes are optional
 * 3) Types are identified by name and explicitly registered
 * 4) Most types require very little boiler plate code
 * 5) Properties work very differently.  In gobject, all properties are
 *    implemented via an overridden virtual function using a select statement.
 *    Properties are indexed with integers and registered with the class.
 *    Native type setter/getters are optional.  GValue variants are used to
 *    encode the types.  The type is registered with the property and then the
 *    implementation must use the appropriate GValue function.
 *
 *    In QType, native type getter/setters are required.  Properties are
 *    registered with an instance.  Properties are indexed by string name.
 *    Code generation is used to dispatch from the generic marshalling frame
 *    work to native type getter/setters.
 * 6) Signals are not currently implemented.
 */

typedef uint64_t Type;

typedef struct TypeClass
{
    Type type;
} TypeClass;

#define MAX_ID (32 + 1)

typedef struct TypeInstance
{
    TypeClass *class;

    char id[MAX_ID];
} TypeInstance;

typedef struct TypeInfo
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
} TypeInfo;

Type type_register_static(const TypeInfo *info);

void type_initialize(void *obj, const char *typename, const char *id);

void type_finalize(void *obj);

TypeInstance *type_new(const char *typename, const char *id);

TypeInstance *type_check_type(TypeInstance *obj, const char *typename);

TypeInstance *type_find_by_id(const char *id);

TypeClass *type_check_class(TypeClass *obj, const char *typename);

TypeClass *type_get_class(TypeInstance *obj);

#define TYPE_INSTANCE(obj) ((TypeInstance *)(obj))
#define TYPE_CHECK(type, obj, name) ((type *)type_check_type((TypeInstance *)(obj), (name)))
#define TYPE_CLASS_CHECK(class, obj, name) ((class *)type_check_class((TypeClass *)(obj), (name)))

Type type_get_by_name(const char *name);

const char *type_get_id(TypeInstance *obj);
const char *type_get_type(TypeInstance *obj);

void type_foreach(void (*enumfn)(TypeInstance *obj, void *opaque), void *opaque);

const char *type_get_name(Type type);

TypeClass *type_get_super(TypeInstance *obj);

#endif
