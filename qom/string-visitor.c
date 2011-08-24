#include "string-visitor.h"

static void string_input_visitor_int(Visitor *v, int64_t *obj, const char *name, Error **errp)
{
    StringInputVisitor *sv = container_of(v, StringInputVisitor, parent);
    *obj = atoi(sv->value);
}

static void string_input_visitor_str(Visitor *v, char **obj, const char *name, Error **errp)
{
    StringInputVisitor *sv = container_of(v, StringInputVisitor, parent);
    *obj = g_strdup(sv->value);
}

void string_input_visitor_init(StringInputVisitor *sv, const char *value)
{
    memset(sv, 0, sizeof(*sv));
    sv->parent.type_int = string_input_visitor_int;
    sv->parent.type_str = string_input_visitor_str;
    sv->value = value;
}

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

void string_output_visitor_init(StringOutputVisitor *sv)
{
    memset(sv, 0, sizeof(*sv));
    sv->parent.type_int = string_output_visitor_int;
    sv->parent.type_str = string_output_visitor_str;
}
