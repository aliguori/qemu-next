typedef struct Plug Plug;
typedef struct Visitor Visitor;
typedef struct Error Error;

#include "variant.h"
#include <stdio.h>

static void plug_add_property_full(Plug *plug, const char *name,
                                   const char *type,
                                   EtterTrampoline *getter,
                                   EtterTrampoline *setter)
{
    printf("%s %s\n", name, type);
}

static void foo_set_bar(Plug *plug, int bar)
{
}

static int foo_get_bar(Plug *plug)
{
    return 32;
}

int main(int argc, char **argv)
{
    printf("%s\n", typename(int8_t));
    printf("%s\n", typename(int16_t));
    printf("%s\n", typename(int32_t));
    printf("%s\n", typename(int64_t));
    printf("%s\n", typename(uint8_t));
    printf("%s\n", typename(uint16_t));
    printf("%s\n", typename(uint32_t));
    printf("%s\n", typename(uint64_t));
    printf("%s\n", typename(char *));
    printf("%s\n", typename(void *));

    plug_add_property(0, "bar", int, &foo_get_bar, &foo_set_bar);

    return 0;
}
