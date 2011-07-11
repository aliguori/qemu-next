#include "qemu/type.h"
#include <glib.h>

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
} TypeImpl;

static int num_types = 1;
static TypeImpl type_table[128];

Type type_register_static(const TypeInfo *info)
{
    Type type = num_types++;
    TypeImpl *ti;

    ti = &type_table[type];

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

static void type_class_init(TypeImpl *ti)
{
    size_t class_size = sizeof(TypeClass);

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

    if (ti->class_init) {
        ti->class_init(ti->class);
    }
}

static void type_instance_init(TypeInstance *obj, const char *typename)
{
    TypeImpl *ti = type_get_instance(type_get_by_name(typename));

    if (ti->parent) {
        type_instance_init(obj, ti->parent);
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

TypeInstance *type_check_type(TypeInstance *obj, const char *typename)
{
    Type target_type = type_get_by_name(typename);
    Type type = obj->class->type;

    while (type) {
        TypeImpl *ti = type_get_instance(type);

        if (ti->type == target_type) {
            return obj;
        }

        type = type_get_by_name(ti->parent);
    }

    fprintf(stderr, "Object %p is not an instance of type %d\n", obj, (int)type);
    abort();

    return NULL;
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
