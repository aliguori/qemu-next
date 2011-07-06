#ifndef PLUG_H
#define PLUG_H

#include "qemu-common.h"

int plug_create_from_kv(int argc, const char *names[], const char *values[]);

int plug_create_from_va(const char *driver, const char *id, va_list ap);

int plug_create_from_string(const char *optarg);

int plug_create(const char *driver, const char *id, ...);

#endif
