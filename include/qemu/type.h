/*
 * QEMU Object Model
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_TYPE_H
#define QEMU_TYPE_H

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * We very purposefully don't include any QEMU include files here.  This file
 * will be included by *everything* so any make file dependency here will ensure
 * the entire tree rebuilds if those dependencies change.
 */

#define MAX_ID (32 + 1)

typedef uint64_t Type;

typedef struct TypeClass TypeClass;
typedef struct TypeInstance TypeInstance;
typedef struct TypeInfo TypeInfo;

typedef struct InterfaceClass InterfaceClass;
typedef struct Interface Interface;
typedef struct InterfaceInfo InterfaceInfo;

/**
 * @TypeClass:
 *
 * The base for all classes.  The only thing that @TypeClass contains is an
 * integer type handle.
 */
struct TypeClass
{
    /**
     * @type the handle of the type for a class
     */
    Type type;
};

/**
 * @TypeInstance:
 *
 * The base for all objects.  The first member of this object is a pointer to
 * a @TypeClass.  Since C guarantees that the first member of a structure
 * always begins at byte 0 of that structure, as long as any sub-object places
 * its parent as the first member, we can cast directly to a @TypeInstance.
 *
 * As a result, @TypeInstance contains a reference to the objects type as its
 * first member.  This allows identification of the real type of the object at
 * run time.
 *
 * @TypeInstance also contains a list of @Interfaces that this object
 * implements.
 */
struct TypeInstance
{
    /**
     * @class the type of the instantiated object.
     */
    TypeClass *class;

    /**
     * @id the name of the object
     */
    char id[MAX_ID];

    /**
     * @interfaces a list of @Interface objects implemented by this object
     */
    GSList *interfaces;
};

/**
 * @TypeInfo:
 *
 */
struct TypeInfo
{
    /**
     * @name the name of the type
     */
    const char *name;

    /**
     * @parent the name of the parent type
     */
    const char *parent;

    /**
     * Instance Initialization
     *
     * This functions manage the instance construction and destruction of a
     * type.
     */

    /**
     * @instance_size the size of the object (derivative of @TypeInstance).  If
     * @instance_size is 0, then the size of the object will be the size of the
     * parent object.
     */
    size_t instance_size;

    /**
     * @instance_init
     *
     * This function is called to initialize an object.  The parent class will
     * have already been initialized so the type is only responsible for
     * initializing its own members.
     */
    void (*instance_init)(TypeInstance *obj);

    /**
     * @instance_finalize
     *
     * This function is called during object destruction.  This is called before
     * the parent @instance_finalize function has been called.  An object should
     * only free the members that are unique to its type in this function.
     */
    void (*instance_finalize)(TypeInstance *obj);

    /**
     * @abstract
     *
     * If this field is true, then the class is considered abstract and cannot
     * be directly instantiated.
     */
    bool abstract;

    /**
     * Class Initialization
     *
     * Before an object is initialized, the class for the object must be
     * initialized.  There is only one class object for all instance objects
     * that is created lazily.
     *
     * Classes are initialized by first initializing any parent classes (if
     * necessary).  After the parent class object has initialized, it will be
     * copied into the current class object and any additional storage in the
     * class object is zero filled.
     *
     * The effect of this is that classes automatically inherit any virtual
     * function pointers that the parent class has already initialized.  All
     * other fields will be zero filled.
     *
     * After this initial copy, @base_init is invoked.  This is meant to handle
     * the case where a class may have a dynamic field that was copied via
     * a shallow copy but needs to be deep copied.  @base_init is called for
     * each parent class but not for the class being instantiated.
     *
     * Once all of the parent classes have been initialized and their @base_init
     * functions have been called, @class_init is called to let the class being
     * instantiated provide default initialize for it's virtual functions.
     */

    /**
     * @class_size the size of the class object (derivative of @TypeClass) for
     * this object.  If @class_size is 0, then the size of the class will be
     * assumed to be the size of the parent class.  This allows a type to avoid
     * implementing an explicit class type if they are not adding additional
     * virtual functions.
     */
    size_t class_size;

    /**
     * @base_init
     *
     * This function is called after memcpy()'ing the base class into the new
     * class to reinitialize any members that require deep copy.
     */
    void (*base_init)(TypeClass *klass);

    /**
     * @base_finalize
     *
     * This function is called during a class's destruction and is meant to
     * allow any dynamic parameters allocated by @base_init to be released.
     */
    void (*base_finalize)(TypeClass *klass);

    /**
     * @class_init
     *
     * This function is called after all parent class initialization has occured
     * to allow a class to set its default virtual method pointers.  This is
     * also the function to use to override virtual methods from a parent class.
     */
    void (*class_init)(TypeClass *klass);

    /**
     * @class_finalize
     *
     * This function is called during class destruction and is meant to release
     * and dynamic parameters allocated by @class_init.
     */
    void (*class_finalize)(TypeClass *klass);

    /**
     * Interfaces
     *
     * Interfaces allow a limited form of multiple inheritance.  Instances are
     * similar to normal types except for the fact that are only defined by
     * their classes and never carry any state.  You can cast an object to one
     * of its @Interface types and vice versa.
     */

    /**
     * @interfaces the list of interfaces associated with this type.  This
     * should point to a static array that's terminated with a zero filled
     * element.
     */
    InterfaceInfo *interfaces;
};

/**
 * @TYPE_INSTANCE
 *
 * Converts an object to a @TypeInstance.  Since all objects are @TypeInstances,
 * this function will always succeed.
 */
#define TYPE_INSTANCE(obj) \
    ((TypeInstance *)(obj))

/**
 * @TYPE_CHECK
 *
 * A type safe version of @type_dynamic_cast_assert.  Typically each class will
 * define a macro based on this type to perform type safe dynamic_casts to
 * this object type.
 *
 * If an invalid object is passed to this function, a run time assert will be
 * generated.
 */
#define TYPE_CHECK(type, obj, name) \
    ((type *)type_dynamic_cast_assert((TypeInstance *)(obj), (name)))

/**
 * @TYPE_CLASS_CHECK
 *
 * A type safe version of @type_check_class.  This macro is typically wrapped
 * by each type to perform type safe casts of a class to a specific class type.
 */
#define TYPE_CLASS_CHECK(class, obj, name) \
    ((class *)type_check_class((TypeClass *)(obj), (name)))

/**
 * @TYPE_GET_CLASS
 *
 * This function will return a specific class for a given object.  Its generally
 * used by each type to provide a type safe macro to get a specific class type
 * from an object.
 */
#define TYPE_GET_CLASS(class, obj, name) \
    TYPE_CLASS_CHECK(class, type_get_class(TYPE_INSTANCE(obj)), name)

/**
 * @Interface:
 *
 * The base for all Interfaces.  This is a subclass of TypeInstance.  Subclasses
 * of @Interface should never have an instance that contains anything other than
 * a single @Interface member.
 */ 
struct Interface
{
    /**
     * @parent base class
     */
    TypeInstance parent;

    /* private */

    /**
     * @obj a pointer to the object that implements this interface.  This is
     * used to allow casting from an interface to the base object.
     */
    TypeInstance *obj;
};

/**
 * @InterfaceClass:
 *
 * The class for all interfaces.  Subclasses of this class should only add
 * virtual methods.
 */
struct InterfaceClass
{
    /**
     * @parent_class the base class
     */
    TypeClass parent_class;
};

/**
 * @InterfaceInfo:
 *
 * The information associated with an interface.
 */
struct InterfaceInfo
{
    /**
     * @type the name of the interface
     */
    const char *type;

    /**
     * @interface_initfn is called during class initialization and is used to
     * initialize an interface associated with a class.  This function should
     * initialize any default virtual functions for a class and/or override
     * virtual functions in a parent class.
     */
    void (*interface_initfn)(TypeClass *class);
};

#define TYPE_INTERFACE "interface"
#define INTERFACE(obj) TYPE_CHECK(Interface, obj, TYPE_INTERFACE)

/**
 * @type_new:
 *
 * This function will initialize a new object using heap allocated memory.  This
 * function should be paired with @type_delete to free the resources associated
 * with the object.
 *
 * @typename: The name of the type of the object to instantiate
 *
 * @id:       The id of the object.  This must be unique.
 *
 * Returns:   The newly allocated and instantiated object.
 *
 */
TypeInstance *type_new(const char *typename, const char *id);

/**
 * @type_delete:
 *
 * Finalize an object and then free the memory associated with it.  This should
 * be paired with @type_new to free the resources associated with an object.
 *
 * @obj:  The object to free.
 *
 */
void type_delete(TypeInstance *obj);

/**
 * @type_initialize:
 *
 * This function will initialize an object.  The memory for the object should
 * have already been allocated.
 *
 * @obj:      A pointer to the memory to be used for the object.
 *
 * @typename: The name of the type of the object to instantiate
 *
 * @id:       The id of the object.  This must be unique.
 *
 */
void type_initialize(void *obj, const char *typename, const char *id);

/**
 * @type_finalize:
 *
 * This function destroys and object without freeing the memory associated with
 * it.
 *
 * @obj:
 *
 */
void type_finalize(void *obj);

/**
 * @type_dynamic_cast:
 *
 * This function will determine if @obj is-a @typename.  @obj can refer to an
 * object or an interface associated with an object.
 *
 * @obj:       The object to cast.
 *
 * @typename:  The @typename
 *
 * Returns:
 *
 */
TypeInstance *type_dynamic_cast(TypeInstance *obj, const char *typename);

/**
 * @type_dynamic_cast_assert:
 *
 * @obj:
 *
 * @typename:
 *
 * Returns:
 *
 */
TypeInstance *type_dynamic_cast_assert(TypeInstance *obj, const char *typename);

/**
 * @type_is_type:
 *
 * @obj:
 *
 * @typename:
 *
 * Returns:
 *
 */
bool type_is_type(TypeInstance *obj, const char *typename);

/**
 * @type_get_class:
 *
 * @obj:
 *
 * Returns:
 *
 */
TypeClass *type_get_class(TypeInstance *obj);

/**
 * @type_get_id:
 *
 * @obj:
 *
 * Returns:
 *
 */
const char *type_get_id(TypeInstance *obj);

/**
 * @type_get_type:
 *
 * @obj:
 *
 * Returns:
 */
const char *type_get_type(TypeInstance *obj);

/**
 * @type_get_super:
 *
 * @obj:
 *
 * Returns:
 */
TypeClass *type_get_super(TypeInstance *obj);

/**/

/**
 * @type_register_static:
 *
 * @info:
 *
 * Returns:
 */
Type type_register_static(const TypeInfo *info);

/**
 * @type_find_by_id:
 *
 * @id:
 *
 * Returns:
 *
 */
TypeInstance *type_find_by_id(const char *id);

/**
 * @type_check_class:
 *
 * @obj:
 *
 * @typename:
 *
 * Returns:
 */
TypeClass *type_check_class(TypeClass *obj, const char *typename);

/**
 * @type_get_by_name:
 *
 * @name:
 *
 * Returns:
 */
Type type_get_by_name(const char *name);

/**
 * @type_get_name:
 *
 * @type:
 *
 * Returns:
 */
const char *type_get_name(Type type);

/**
 * @type_foreach:
 *
 * @enumfn:
 *
 * @opaque:
 *
 */
void type_foreach(void (*enumfn)(TypeInstance *obj, void *opaque),
                  void *opaque);

#endif
