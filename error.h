#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>

typedef struct Error Error;

void error_set(Error **err, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

bool error_is_set(Error **err);

const char *error_get_pretty(Error *err);

const char *error_get_field(Error *err, const char *field);

void error_propagate(Error **dst_err, Error *local_err);

void error_free(Error *err);

bool error_is_type(Error *err, const char *fmt);

#endif
