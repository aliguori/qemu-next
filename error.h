#ifndef ERROR_H
#define ERROR_H

#include "qemu-common.h"

typedef struct Error
{
} Error;

void error_set(Error **err, const char *fmt, ...);

bool error_is_set(Error **err);

const char *error_get_pretty(Error *err);

const char *error_get_field(Error *err, const char *field);

void error_free(Error *err);

bool error_is_type(Error *err, const char *fmt);
  
#endif
