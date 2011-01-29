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

const char *error_get_field(Error *err, const char *field)
{
    return "";
}

void error_free(Error *err)
{
}

bool error_is_type(Error *err, const char *fmt)
{
    return false;
}
