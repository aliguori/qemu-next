/*
 * QEMU Errors
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori <aliguori@us.ibm.com>
 *
 */

#include "qemu-common.h"
#include "error.h"

Error *error_new(const char *domain,
                 int code,
                 const char *format,
                 ...)
{
    Error *e;
    va_list ap;

    va_start(ap, format);
    e = error_new_valist(domain, code, format, ap);
    va_end(ap);

    return e;
}

Error *error_new_valist(const char *domain,
                        int code,
                        const char *format,
                        va_list ap)
{
    Error *e;

    e = qemu_mallocz(sizeof(*e));
    e->domain = qemu_strdup(domain);
    e->code = code;

    if (vasprintf(&e->message, format, ap) == -1) {
        abort();
    }

    return e;
}

Error *error_new_literal(const char *domain,
                         int code,
                         const char *message)
{
    Error *e;

    e = qemu_mallocz(sizeof(*e));
    e->domain = qemu_strdup(domain);
    e->code = code;
    e->message = qemu_strdup(message);

    return e;
}

void error_set(Error **errp,
               const char *domain,
               int code,
               const char *format,
               ...)
{
    if (errp) {
        va_list ap;

        va_start(ap, format);
        *errp = error_new_valist(domain, code, format, ap);
        va_end(ap);
    }
}

void error_propagate(Error **errp,
                     Error *err)
{
    if (errp) {
        *errp = err;
    } else {
        error_free(err);
    }
}

Error *error_copy(const Error *err)
{
    return error_new_literal(err->domain,
                             err->code,
                             err->message);
}

void error_free(Error *e)
{
    qemu_free(e->domain);
    free(e->message);
    qemu_free(e);
}

