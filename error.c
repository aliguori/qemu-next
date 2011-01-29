#include "error.h"

void error_set(Error **err, const char *fmt, ...)
{
}

bool error_is_set(Error **err)
{
    return false;
}

const char *error_get_pretty(Error *err)
{
    return "error";
}
