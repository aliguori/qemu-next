#ifndef ERROR_H
#define ERROR_H

typedef struct Error
{
} Error;

void error_set(Error **err, const char *fmt, ...);

const char *error_get_pretty(Error *err);
  
#endif
