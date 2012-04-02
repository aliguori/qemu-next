#include "newemu/i8254.h"

#include <glib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct clock *clock = clock_get_instance();
    struct i8254 s;

    i8254_init(&s, clock);

    i8254_cleanup(&s);

    return 0;
}
