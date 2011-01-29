#ifndef HMP_MARSHAL_HELPERS_H
#define HMP_MARSHAL_HELPERS_H

#include "qemu-common.h"

int hmp_parse_int(const char *value);

target_ulong hmp_parse_targetlong(const char *value);

const char * hmp_parse_str(const char *value);

const char * hmp_parse_blockdev(const char *value);

bool hmp_parse_bool(const char *value);

bool hmp_parse_flag(const char *value);

const char * hmp_parse_file(const char *value);

const char * hmp_parse_gdbfmt(const char *value);

QemuOpts * hmp_parse_opts(const char *value);

int64_t hmp_parse_size(const char *value);

int64_t hmp_parse_time(const char *value);

int64_t hmp_parse_sizemb(const char *value);

#endif
