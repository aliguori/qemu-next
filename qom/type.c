#include "qemu/type.h"
#include <glib.h>

#define MAX_INTERFACES 32

typedef struct InterfaceImpl
{
    const char *parent;
    void (*interface_initfn)(TypeClass *class);
    Type type;
} InterfaceImpl;

typedef struct TypeImpl
{
    const char *name;
    Type type;

    size_t class_size;

    size_t instance_size;

    void (*base_init)(TypeClass *klass);
    void (*base_finalize)(TypeClass *klass);

    void (*class_init)(TypeClass *klass);
    void (*class_finalize)(TypeClass *klass);

    void (*instance_init)(TypeInstance *obj);
    void (*instance_finalize)(TypeInstance *obj);

    const char *parent;

    TypeClass *class;

    int num_interfaces;
    InterfaceImpl interfaces[MAX_INTERFACES];
} TypeImpl;

static int num_types = 1;
static TypeImpl type_table[128];

Type type_register_static(const TypeInfo *info)
{
    Type type = num_types++;
    TypeImpl *ti;

    ti = &type_table[type];

    assert(info->name != NULL);

    ti->name = info->name;
    ti->parent = info->parent;
    ti->type = type;

    ti->class_size = info->class_size;
    ti->instance_size = info->instance_size;

    ti->base_init = info->base_init;
    ti->base_finalize = info->base_finalize;

    ti->class_init = info->class_init;
    ti->class_finalize = info->class_finalize;

    ti->instance_init = info->instance_init;
    ti->instance_finalize = info->instance_finalize;

    if (info->interfaces) {
        int i;

        for (i = 0; info->interfaces[i].type; i++) {
            ti->interfaces[i].parent = info->interfaces[i].type;
            ti->interfaces[i].interface_initfn = info->interfaces[i].interface_initfn;
            ti->num_interfaces++;
        }
    }

    return type;
}

static Type type_register_anonymous(const TypeInfo *info)
{
    Type type = num_types++;
    TypeImpl *ti;
    char buffer[32];
    static int count;

    ti = &type_table[type];

    snprintf(buffer, sizeof(buffer), "<anonymous-%d>", count++);
    ti->name = qemu_strdup(buffer);
    ti->parent = qemu_strdup(info->parent);
    ti->type = type;

    ti->class_size = info->class_size;
    ti->instance_size = info->instance_size;

    ti->base_init = info->base_init;
    ti->base_finalize = info->base_finalize;

    ti->class_init = info->class_init;
    ti->class_finalize = info->class_finalize;

    ti->instance_init = info->instance_init;
    ti->instance_finalize = info->instance_finalize;

    if (info->interfaces) {
        int i;

        for (i = 0; info->interfaces[i].type; i++) {
            ti->interfaces[i].parent = info->interfaces[i].type;
            ti->interfaces[i].interface_initfn = info->interfaces[i].interface_initfn;
            ti->num_interfaces++;
        }
    }

    return type;
}

static TypeImpl *type_get_instance(Type type)
{
    assert(type != 0);
    assert(type < num_types);

    return &type_table[type];
}

Type type_get_by_name(const char *name)
{
    int i;

    if (name == NULL) {
        return 0;
    }

    for (i = 1; i < num_types; i++) {
        if (strcmp(name, type_table[i].name) == 0) {
            return i;
        }
    }

    return 0;
}

static void type_class_base_init(TypeImpl *base_ti, const char *typename)
{
    TypeImpl *ti;

    if (!typename) {
        return;
    }

    ti = type_get_instance(type_get_by_name(typename));

    type_class_base_init(base_ti, ti->parent);

    if (ti->base_init) {
        ti->base_init(base_ti->class);
    }
}

static size_t type_class_get_size(TypeImpl *ti)
{
    if (ti->class_size) {
        return ti->class_size;
    }

    if (ti->parent) {
        return type_class_get_size(type_get_instance(type_get_by_name(ti->parent)));
    }

    return sizeof(TypeClass);
}

static void type_class_interface_init(TypeImpl *ti, InterfaceImpl *iface)
{
    TypeInfo info = {
        .instance_size = sizeof(Interface),
        .parent = iface->parent,
        .class_size = sizeof(InterfaceClass),
        .class_init = iface->interface_initfn,
    };

    iface->type = type_register_anonymous(&info);
}

static void type_class_init(TypeImpl *ti)
{
    size_t class_size = sizeof(TypeClass);
    int i;

    if (ti->class) {
        return;
    }

    ti->class_size = type_class_get_size(ti);

    ti->class = qemu_malloc(ti->class_size);
    ti->class->type = ti->type;

    if (ti->parent) {
        TypeImpl *ti_parent;

        ti_parent = type_get_instance(type_get_by_name(ti->parent));

        type_class_init(ti_parent);

        class_size = ti_parent->class_size;
        assert(ti_parent->class_size <= ti->class_size);

        memcpy((void *)ti->class + sizeof(TypeClass),
               (void *)ti_parent->class + sizeof(TypeClass),
               ti_parent->class_size - sizeof(TypeClass));
    }

    memset((void *)ti->class + class_size, 0, ti->class_size - class_size);

    type_class_base_init(ti, ti->parent);

    for (i = 0; i < ti->num_interfaces; i++) {
        type_class_interface_init(ti, &ti->interfaces[i]);
    }

    if (ti->class_init) {
        ti->class_init(ti->class);
    }
}

static void type_instance_interface_init(TypeInstance *obj, InterfaceImpl *iface)
{
    TypeImpl *ti = type_get_instance(iface->type);
    Interface *iface_obj;
    static int count;
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "__anonymous_%d", count++);
    iface_obj = INTERFACE(type_new(ti->name, buffer));
    iface_obj->obj = obj;

    obj->interfaces = g_slist_prepend(obj->interfaces, iface_obj);
}

static void type_instance_init(TypeInstance *obj, const char *typename)
{
    TypeImpl *ti = type_get_instance(type_get_by_name(typename));
    int i;

    if (ti->parent) {
        type_instance_init(obj, ti->parent);
    }

    for (i = 0; i < ti->num_interfaces; i++) {
        type_instance_interface_init(obj, &ti->interfaces[i]);
    }

    if (ti->instance_init) {
        ti->instance_init(obj);
    }
}

static GHashTable *global_object_table;

void type_initialize(void *data, const char *typename, const char *id)
{
    TypeImpl *ti = type_get_instance(type_get_by_name(typename));
    TypeInstance *obj = data;

    assert(ti->instance_size >= sizeof(TypeClass));

    type_class_init(ti);

    memset(obj, 0, ti->instance_size);

    obj->class = ti->class;
    snprintf(obj->id, sizeof(obj->id), "%s", id);

    if (global_object_table == NULL) {
        global_object_table = g_hash_table_new(g_str_hash, g_str_equal);
    }

    g_hash_table_insert(global_object_table, obj->id, obj);

    type_instance_init(obj, typename);
}

static void type_instance_finalize(TypeInstance *obj, const char *typename)
{
    TypeImpl *ti = type_get_instance(type_get_by_name(typename));

    if (ti->instance_finalize) {
        ti->instance_finalize(obj);
    }

    while (obj->interfaces) {
        Interface *iface_obj = obj->interfaces->data;
        obj->interfaces = g_slist_delete_link(obj->interfaces, obj->interfaces);
        type_delete(TYPE_INSTANCE(iface_obj));
    }

    if (ti->parent) {
        type_instance_init(obj, ti->parent);
    }
}

void type_finalize(void *data)
{
    TypeInstance *obj = data;
    TypeImpl *ti = type_get_instance(obj->class->type);

    g_hash_table_remove(global_object_table, obj->id);

    type_instance_finalize(obj, ti->name);
}

const char *type_get_name(Type type)
{
    TypeImpl *ti = type_get_instance(type);
    return ti->name;
}

TypeInstance *type_new(const char *typename, const char *id)
{
    TypeImpl *ti = type_get_instance(type_get_by_name(typename));
    TypeInstance *obj;

    obj = qemu_malloc(ti->instance_size);
    type_initialize(obj, typename, id);

    return obj;
}

void type_delete(TypeInstance *obj)
{
    type_finalize(obj);
    qemu_free(obj);
}

TypeInstance *type_find_by_id(const char *id)
{
    gpointer data;

    if (global_object_table == NULL) {
        return NULL;
    }

    data = g_hash_table_lookup(global_object_table, id);

    if (!data) {
        return NULL;
    }

    return TYPE_INSTANCE(data);
}

bool type_is_type(TypeInstance *obj, const char *typename)
{
    Type target_type = type_get_by_name(typename);
    Type type = obj->class->type;

    /* Check if typename is a direct ancestor of type */
    while (type) {
        TypeImpl *ti = type_get_instance(type);

        if (ti->type == target_type) {
            return true;
        }

        type = type_get_by_name(ti->parent);
    }

    return false;
}

TypeInstance *type_dynamic_cast(TypeInstance *obj, const char *typename)
{
    GSList *i;

    /* Check if typename is a direct ancestor */
    if (type_is_type(obj, typename)) {
        return obj;
    }

    /* Check if obj has an interface of typename */
    for (i = obj->interfaces; i; i = i->next) {
        Interface *iface = i->data;

        if (type_is_type(TYPE_INSTANCE(iface), typename)) {
            return TYPE_INSTANCE(iface);
        }
    }

    /* Check if obj is an interface and it's containing object is a direct ancestor of typename */
    if (type_is_type(obj, TYPE_INTERFACE)) {
        Interface *iface = INTERFACE(obj);

        if (type_is_type(iface->obj, typename)) {
            return iface->obj;
        }
    }

    return NULL;
}


static void register_interface(void)
{
    static TypeInfo interface_info = {
        .name = TYPE_INTERFACE,
        .instance_size = sizeof(Interface),
    };

    type_register_static(&interface_info);
}

device_init(register_interface);

TypeInstance *type_dynamic_cast_assert(TypeInstance *obj, const char *typename)
{
    TypeInstance *inst;

    inst = type_dynamic_cast(obj, typename);

    if (!inst) {
        fprintf(stderr, "Object %p is not an instance of type %s\n", obj, typename);
        abort();
    }

    return inst;
}

TypeClass *type_check_class(TypeClass *class, const char *typename)
{
    Type target_type = type_get_by_name(typename);
    Type type = class->type;

    while (type) {
        TypeImpl *ti = type_get_instance(type);

        if (ti->type == target_type) {
            return class;
        }

        type = type_get_by_name(ti->parent);
    }

    fprintf(stderr, "Object %p is not an instance of type %d\n", class, (int)type);
    abort();

    return NULL;
}

const char *type_get_id(TypeInstance *obj)
{
    return obj->id;
}

const char *type_get_type(TypeInstance *obj)
{
    return type_get_name(obj->class->type);
}

TypeClass *type_get_class(TypeInstance *obj)
{
    return obj->class;
}

typedef struct TypeForeachData
{
    void (*enumfn)(TypeInstance *obj, void *opaque);
    void *opaque;
} TypeForeachData;

static void type_foreach_tramp(gpointer key, gpointer value, gpointer opaque)
{
    TypeForeachData *data = opaque;
    data->enumfn(TYPE_INSTANCE(value), data->opaque);
}

void type_foreach(void (*enumfn)(TypeInstance *obj, void *opaque), void *opaque)
{
    TypeForeachData data = {
        .enumfn = enumfn,
        .opaque = opaque,
    };

    g_hash_table_foreach(global_object_table, type_foreach_tramp, &data);
}

TypeClass *type_get_super(TypeInstance *obj)
{
    return type_get_instance(type_get_by_name(type_get_instance(obj->class->type)->parent))->class;
}

