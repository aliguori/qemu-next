#ifndef ERROR_H
#define ERROR_H

#include "qemu-common.h"

typedef struct Error
{
} Error;

void error_set(Error **err, const char *fmt, ...);

bool error_is_set(Error **err);

const char *error_get_pretty(Error *err);

  
#endif
