#ifndef STRING_VISITOR_H
#define STRING_VISITOR_H

#include "qapi/qapi-visit-core.h"

typedef struct StringInputVisitor
{
    Visitor parent;
    const char *value;
} StringInputVisitor;

typedef struct StringOutputVisitor
{
    Visitor parent;
    char value[256];
} StringOutputVisitor;

void string_input_visitor_init(StringInputVisitor *sv, const char *value);
void string_output_visitor_init(StringOutputVisitor *sv);

#endif
