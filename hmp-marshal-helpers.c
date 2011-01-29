#include "hmp-marshal-helpers.h"

/* FIXME support parsing errors */

int hmp_parse_int(const char *value)
{
    return strtol(value, NULL, 0);
}

target_ulong hmp_parse_targetlong(const char *value)
{
    return strtoull(value, NULL, 0);
}

const char * hmp_parse_str(const char *value)
{
    return value;
}

const char * hmp_parse_blockdev(const char *value)
{
    return value;
}

bool hmp_parse_bool(const char *value)
{
    if (strcmp(value, "on") == 1) {
        return true;
    }

    return false;
}

bool hmp_parse_flag(const char *value)
{
    return true;
}

const char * hmp_parse_file(const char *value)
{
    return value;
}

const char * hmp_parse_gdbfmt(const char *value)
{
    return value;
}

QemuOpts * hmp_parse_opts(const char *value)
{
    return NULL;
}

int64_t hmp_parse_size(const char *value)
{
    return strtoll(value, NULL, 0);
}

int64_t hmp_parse_time(const char *value)
{
    return strtoll(value, NULL, 0);
}

int64_t hmp_parse_sizemb(const char *value)
{
    return strtoll(value, NULL, 0);
}
