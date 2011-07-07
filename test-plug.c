/** stub **/
#include "plug.h"

static int nb_module_init;
static void (*module_initfn[100])(void);

void register_module_init(void (*fn)(void), module_init_type type)
{
    int i = nb_module_init++;
    module_initfn[i] = fn;
}

typedef struct TestPlug
{
    Plug parent;
    int x;
    Plug child;
    Plug *slot;
} TestPlug;

#define TYPE_TEST_PLUG "test-plug"
#define TEST_PLUG(obj) TYPE_CHECK(TestPlug, obj, TYPE_TEST_PLUG)

static void test_plug_initialize(TestPlug *obj, const char *id)
{
    type_initialize(obj, TYPE_TEST_PLUG, id);
}

static int64_t test_plug_get_x(Plug *obj)
{
    TestPlug *tp = TEST_PLUG(obj);

    return tp->x;
}

static void test_plug_set_x(Plug *obj, int64_t value)
{
    TestPlug *tp = TEST_PLUG(obj);
    tp->x = value;
}

static void test_plug_initfn(TypeInstance *obj)
{
    TestPlug *tp = TEST_PLUG(obj);
    char name[32];

    snprintf(name, sizeof(name), "%s::child", obj->id);
    plug_initialize(&tp->child, name);

    tp->x = 42;
    plug_add_property_int(PLUG(tp), "x", test_plug_get_x, test_plug_set_x, PROP_F_READWRITE);
    plug_add_property_plug(PLUG(tp), "child", &tp->child, TYPE_PLUG);
    plug_add_property_socket(PLUG(tp), "slot", &tp->slot, TYPE_PLUG, PROP_F_READWRITE | PROP_F_MASKABLE);
}

static const TypeInfo test_plug_type_info = {
    .name = TYPE_TEST_PLUG,
    .parent = TYPE_PLUG,
    .instance_size = sizeof(TestPlug),
    .instance_init = test_plug_initfn,
};

static void register_devices(void)
{
    type_register_static(&test_plug_type_info);
}

device_init(register_devices);

//////////////////////////////

typedef struct StringInputVisitor
{
    Visitor parent;
    const char *value;
} StringInputVisitor;

static void string_input_visitor_int(Visitor *v, int64_t *obj, const char *name, Error **errp)
{
    StringInputVisitor *sv = container_of(v, StringInputVisitor, parent);
    *obj = atoi(sv->value);
}

static void string_input_visitor_str(Visitor *v, char **obj, const char *name, Error **errp)
{
    StringInputVisitor *sv = container_of(v, StringInputVisitor, parent);
    *obj = qemu_strdup(sv->value);
}

static void string_input_visitor_init(StringInputVisitor *sv, const char *value)
{
    memset(sv, 0, sizeof(*sv));
    sv->parent.type_int = string_input_visitor_int;
    sv->parent.type_str = string_input_visitor_str;
    sv->value = value;
}

typedef struct StringOutputVisitor
{
    Visitor parent;
    char value[256];
} StringOutputVisitor;

static void string_output_visitor_int(Visitor *v, int64_t *obj, const char *name, Error **errp)
{
    StringOutputVisitor *sv = container_of(v, StringOutputVisitor, parent);
    snprintf(sv->value, sizeof(sv->value), "%" PRId64, *obj);
}

static void string_output_visitor_str(Visitor *v, char **obj, const char *name, Error **errp)
{
    StringOutputVisitor *sv = container_of(v, StringOutputVisitor, parent);

    snprintf(sv->value, sizeof(sv->value), "%s", *obj);
}

static void string_output_visitor_init(StringOutputVisitor *sv)
{
    memset(sv, 0, sizeof(*sv));
    sv->parent.type_int = string_output_visitor_int;
    sv->parent.type_str = string_output_visitor_str;
}

static void print_props(Plug *plug, const char *name, const char *typename, int flags, void *opaque)
{
    StringOutputVisitor sov;

    string_output_visitor_init(&sov);
    plug_get_property(plug, name, &sov.parent, NULL);
    printf("`%s.%s' is a `%s' and has a value of `%s', %s\n", plug_get_id(plug), name, typename, sov.value,
           ((flags & PROP_F_READWRITE) == PROP_F_READWRITE) ? "rw" : "ro"); // FIXME
}

int main(int argc, char **argv)
{
    TestPlug tp;
    int i;
    StringInputVisitor siv;
    StringOutputVisitor sov;

    for (i = 0; i < nb_module_init; i++) {
        module_initfn[i]();
    }

    string_input_visitor_init(&siv, "82");
    string_output_visitor_init(&sov);

    test_plug_initialize(&tp, "tp");

    printf("x - %" PRId64 "\n", test_plug_get_x(PLUG(&tp)));
    plug_set_property(PLUG(&tp), "x", &siv.parent, NULL);

    printf("x - %" PRId64 "\n", test_plug_get_x(PLUG(&tp)));

    plug_get_property(PLUG(&tp), "child", &sov.parent, NULL);
    printf("child - %s\n", sov.value);

    plug_get_property(PLUG(&tp), "slot", &sov.parent, NULL);
    printf("slot - %s\n", sov.value);

    string_input_visitor_init(&siv, "tp::child");
    plug_set_property(PLUG(&tp), "slot", &siv.parent, NULL);

    plug_get_property(PLUG(&tp), "slot", &sov.parent, NULL);
    printf("slot - %s\n", sov.value);

    printf("tp::child - %p\n", &tp.child);
    printf("tp.slot - %p\n", tp.slot);

    plug_foreach_property(PLUG(&tp), print_props, NULL);

    assert(&tp.child == tp.slot);

    return 0;
}
