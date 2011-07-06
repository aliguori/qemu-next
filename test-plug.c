/** stub **/
#include "plug.h"

static int nb_module_init;
static void (*module_initfn[100])(void);

void register_module_init(void (*fn)(void), module_init_type type)
{
    int i = nb_module_init++;
    module_initfn[i] = fn;
}

static int64_t value = 42;

static int64_t plug_get_x(Plug *plug)
{
    return value;
}

static void plug_set_x(Plug *plug, int64_t v)
{
    value = v;
}

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

static void string_input_visitor_init(StringInputVisitor *sv, const char *value)
{
    memset(sv, 0, sizeof(*sv));
    sv->parent.type_int = string_input_visitor_int;
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

static void string_output_visitor_init(StringOutputVisitor *sv)
{
    memset(sv, 0, sizeof(*sv));
    sv->parent.type_int = string_output_visitor_int;
}

int main(int argc, char **argv)
{
    Plug plug;
    int i;
    StringInputVisitor siv;
    StringOutputVisitor sov;

    for (i = 0; i < nb_module_init; i++) {
        module_initfn[i]();
    }

    string_input_visitor_init(&siv, "82");
    string_output_visitor_init(&sov);

    plug_initialize(&plug);
    plug_add_property_int(&plug, "x", plug_get_x, plug_set_x);

    printf("x - %" PRId64 "\n", plug_get_x(&plug));
    plug_set_property(&plug, "x", &siv.parent, NULL);

    printf("x - %" PRId64 "\n", plug_get_x(&plug));

    return 0;
}
