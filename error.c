#include "error.h"
#include "error_int.h"
#include "qemu-objects.h"
#include "qerror.h"
#include <glib.h>

struct Error
{
    QDict *obj;
    const char *fmt;
    char *msg;
};

void error_set(Error **errp, const char *fmt, ...)
{
    Error *err;
    va_list ap;

    if (errp == NULL) {
        return;
    }

    err = qemu_mallocz(sizeof(*err));

    va_start(ap, fmt);
    err->obj = qobject_to_qdict(qobject_from_jsonv(fmt, &ap));
    va_end(ap);
    err->fmt = fmt;

    *errp = err;
}

bool error_is_set(Error **errp)
{
    return (errp && *errp);
}

const char *error_get_pretty(Error *err)
{
    if (err->msg == NULL) {
        QString *str;
        str = qerror_format(err->fmt, err->obj);
        err->msg = qemu_strdup(qstring_get_str(str));
    }

    return err->msg;
}

const char *error_get_field(Error *err, const char *field)
{
    if (strcmp(field, "class") == 0) {
        return qdict_get_str(err->obj, field);
    } else {
        QDict *dict = qdict_get_qdict(err->obj, "data");
        return qdict_get_str(dict, field);
    }
}

void error_free(Error *err)
{
    QDECREF(err->obj);
    qemu_free(err->msg);
    qemu_free(err);
}

bool error_is_type(Error *err, const char *fmt)
{
    char *ptr;
    char *end;
    char classname[1024];

    ptr = strstr(fmt, "'class': '");
    g_assert(ptr != NULL);
    ptr += strlen("'class': '");

    end = strchr(ptr, '\'');
    g_assert(end != NULL);
    
    memcpy(classname, ptr, (end - ptr));
    classname[(end - ptr)] = 0;

    return strcmp(classname, error_get_field(err, "class")) == 0;
}

void error_propagate(Error **dst_err, Error *local_err)
{
    if (dst_err) {
        *dst_err = local_err;
    } else if (local_err) {
        error_free(local_err);
    }
}

QObject *error_get_qobject(Error *err)
{
    QINCREF(err->obj);
    return QOBJECT(err->obj);
}

void error_set_qobject(Error **errp, QObject *obj)
{
    Error *err;
    if (errp == NULL) {
        return;
    }
    err = qemu_mallocz(sizeof(*err));
    err->obj = qobject_to_qdict(obj);
    qobject_incref(obj);

    *errp = err;
}
