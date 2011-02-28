/*
 * QError Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "monitor.h"
#include "qjson.h"
#include "qerror.h"
#include "qemu-common.h"

static void qerror_destroy_obj(QObject *obj);

static const QType qerror_type = {
    .code = QTYPE_QERROR,
    .destroy = qerror_destroy_obj,
};

/**
 * The 'desc' parameter is a printf-like string, the format of the format
 * string is:
 *
 * %(KEY)
 *
 * Where KEY is a QDict key, which has to be passed to qerror_from_info().
 *
 * Example:
 *
 * "foo error on device: %(device) slot: %(slot_nr)"
 *
 * A single percent sign can be printed if followed by a second one,
 * for example:
 *
 * "running out of foo: %(foo)%%"
 *
 * Please keep the entries in alphabetical order.
 * Use "sed -n '/^static.*qerror_table\[\]/,/^};/s/QERR_/&/gp' qerror.c | sort -c"
 * to check.
 */
static const QErrorStringTable qerror_table[] = {
    {
        .error_fmt = QERR_BAD_BUS_FOR_DEVICE,
        .desc      = "Device '%(device)' can't go on a %(bad_bus_type) bus",
    },
    {
        .error_fmt = QERR_BUS_NOT_FOUND,
        .desc      = "Bus '%(bus)' not found",
    },
    {
        .error_fmt = QERR_BUS_NO_HOTPLUG,
        .desc      = "Bus '%(bus)' does not support hotplugging",
    },
    {
        .error_fmt = QERR_COMMAND_NOT_FOUND,
        .desc      = "The command %(name) has not been found",
    },
    {
        .error_fmt = QERR_DEVICE_ENCRYPTED,
        .desc      = "Device '%(device)' (%(encrypted_filename)) is encrypted",
    },
    {
        .error_fmt = QERR_DEVICE_INIT_FAILED,
        .desc      = "Device '%(device)' could not be initialized",
    },
    {
        .error_fmt = QERR_DEVICE_IN_USE,
        .desc      = "Device '%(device)' is in use",
    },
    {
        .error_fmt = QERR_DEVICE_LOCKED,
        .desc      = "Device '%(device)' is locked",
    },
    {
        .error_fmt = QERR_DEVICE_MULTIPLE_BUSSES,
        .desc      = "Device '%(device)' has multiple child busses",
    },
    {
        .error_fmt = QERR_DEVICE_NOT_ACTIVE,
        .desc      = "Device '%(device)' has not been activated",
    },
    {
        .error_fmt = QERR_DEVICE_NOT_ENCRYPTED,
        .desc      = "Device '%(device)' is not encrypted",
    },
    {
        .error_fmt = QERR_DEVICE_NOT_FOUND,
        .desc      = "Device '%(device)' not found",
    },
    {
        .error_fmt = QERR_DEVICE_NOT_REMOVABLE,
        .desc      = "Device '%(device)' is not removable",
    },
    {
        .error_fmt = QERR_DEVICE_NO_BUS,
        .desc      = "Device '%(device)' has no child bus",
    },
    {
        .error_fmt = QERR_DEVICE_NO_HOTPLUG,
        .desc      = "Device '%(device)' does not support hotplugging",
    },
    {
        .error_fmt = QERR_DUPLICATE_ID,
        .desc      = "Duplicate ID '%(id)' for %(object)",
    },
    {
        .error_fmt = QERR_FD_NOT_FOUND,
        .desc      = "File descriptor named '%(name)' not found",
    },
    {
        .error_fmt = QERR_FD_NOT_SUPPLIED,
        .desc      = "No file descriptor supplied via SCM_RIGHTS",
    },
    {
        .error_fmt = QERR_INVALID_BLOCK_FORMAT,
        .desc      = "Invalid block format '%(name)'",
    },
    {
        .error_fmt = QERR_INVALID_PARAMETER,
        .desc      = "Invalid parameter '%(name)'",
    },
    {
        .error_fmt = QERR_INVALID_PARAMETER_TYPE,
        .desc      = "Invalid parameter type, expected: %(expected)",
    },
    {
        .error_fmt = QERR_INVALID_PARAMETER_VALUE,
        .desc      = "Parameter '%(name)' expects %(expected)",
    },
    {
        .error_fmt = QERR_INVALID_PASSWORD,
        .desc      = "Password incorrect",
    },
    {
        .error_fmt = QERR_JSON_PARSING,
        .desc      = "Invalid JSON syntax",
    },
    {
        .error_fmt = QERR_KVM_MISSING_CAP,
        .desc      = "Using KVM without %(capability), %(feature) unavailable",
    },
    {
        .error_fmt = QERR_MIGRATION_EXPECTED,
        .desc      = "An incoming migration is expected before this command can be executed",
    },
    {
        .error_fmt = QERR_MISSING_PARAMETER,
        .desc      = "Parameter '%(name)' is missing",
    },
    {
        .error_fmt = QERR_NO_BUS_FOR_DEVICE,
        .desc      = "No '%(bus)' bus found for device '%(device)'",
    },
    {
        .error_fmt = QERR_OPEN_FILE_FAILED,
        .desc      = "Could not open '%(filename)'",
    },
    {
        .error_fmt = QERR_PROPERTY_NOT_FOUND,
        .desc      = "Property '%(device).%(property)' not found",
    },
    {
        .error_fmt = QERR_PROPERTY_VALUE_BAD,
        .desc      = "Property '%(device).%(property)' doesn't take value '%(value)'",
    },
    {
        .error_fmt = QERR_PROPERTY_VALUE_IN_USE,
        .desc      = "Property '%(device).%(property)' can't take value '%(value)', it's in use",
    },
    {
        .error_fmt = QERR_PROPERTY_VALUE_NOT_FOUND,
        .desc      = "Property '%(device).%(property)' can't find value '%(value)'",
    },
    {
        .error_fmt = QERR_QMP_BAD_INPUT_OBJECT,
        .desc      = "Expected '%(expected)' in QMP input",
    },
    {
        .error_fmt = QERR_QMP_BAD_INPUT_OBJECT_MEMBER,
        .desc      = "QMP input object member '%(member)' expects '%(expected)'",
    },
    {
        .error_fmt = QERR_QMP_EXTRA_MEMBER,
        .desc      = "QMP input object member '%(member)' is unexpected",
    },
    {
        .error_fmt = QERR_SET_PASSWD_FAILED,
        .desc      = "Could not set password",
    },
    {
        .error_fmt = QERR_TOO_MANY_FILES,
        .desc      = "Too many open files",
    },
    {
        .error_fmt = QERR_UNDEFINED_ERROR,
        .desc      = "An undefined error has ocurred",
    },
    {
        .error_fmt = QERR_VNC_SERVER_FAILED,
        .desc      = "Could not start VNC server on %(target)",
    },
    {}
};

/**
 * qerror_new(): Create a new QError
 *
 * Return strong reference.
 */
QError *qerror_new(void)
{
    QError *qerr;

    qerr = qemu_mallocz(sizeof(*qerr));
    QOBJECT_INIT(qerr, &qerror_type);

    return qerr;
}

static void parse_error(const QErrorStringTable *entry, int c)
{
#if 0
    qerror_abort(qerror, "expected '%c' in '%s'", c, entry->desc);
#else
    fprintf(stderr, "expected '%c' in '%s'", c, entry->desc);
    abort();
#endif
}

void qerror_set_desc(QError *qerr, const char *fmt)
{
    int i;

    // FIXME: inefficient loop

    for (i = 0; qerror_table[i].error_fmt; i++) {
        if (strcmp(qerror_table[i].error_fmt, fmt) == 0) {
            qerr->entry = &qerror_table[i];
            return;
        }
    }

#if 0
    qerror_abort(qerr, "error format '%s' not found", fmt);
#else
    abort();
#endif
}

static const char *append_field(QDict *error, QString *outstr,
                                const QErrorStringTable *entry,
                                const char *start)
{
    QObject *obj;
    QDict *qdict;
    QString *key_qs;
    const char *end, *key;

    if (*start != '%')
        parse_error(entry, '%');
    start++;
    if (*start != '(')
        parse_error(entry, '(');
    start++;

    end = strchr(start, ')');
    if (!end)
        parse_error(entry, ')');

    key_qs = qstring_from_substr(start, 0, end - start - 1);
    key = qstring_get_str(key_qs);

    qdict = qobject_to_qdict(qdict_get(error, "data"));
    obj = qdict_get(qdict, key);
    if (!obj) {
#if 0
        qerror_abort(qerror, "key '%s' not found in QDict", key);
#else
        abort();
#endif
    }

    switch (qobject_type(obj)) {
        case QTYPE_QSTRING:
            qstring_append(outstr, qdict_get_str(qdict, key));
            break;
        case QTYPE_QINT:
            qstring_append_int(outstr, qdict_get_int(qdict, key));
            break;
        default:
#if 0
            qerror_abort(qerror, "invalid type '%c'", qobject_type(obj));
#else
            abort();
#endif
    }

    QDECREF(key_qs);
    return ++end;
}

static QString *qerror_format_desc(QDict *error,
                                   const QErrorStringTable *entry)
{
    QString *qstring;
    const char *p;

    assert(entry != NULL);

    qstring = qstring_new();

    for (p = entry->desc; *p != '\0';) {
        if (*p != '%') {
            qstring_append_chr(qstring, *p++);
        } else if (*(p + 1) == '%') {
            qstring_append_chr(qstring, '%');
            p += 2;
        } else {
            p = append_field(error, qstring, entry, p);
        }
    }

    return qstring;
}

QString *qerror_format(const char *fmt, QDict *error)
{
    const QErrorStringTable *entry = NULL;
    int i;

    for (i = 0; qerror_table[i].error_fmt; i++) {
        if (strcmp(qerror_table[i].error_fmt, fmt) == 0) {
            entry = &qerror_table[i];
            break;
        }
    }

    return qerror_format_desc(error, entry);
}

/**
 * qerror_human(): Format QError data into human-readable string.
 *
 * Formats according to member 'desc' of the specified QError object.
 */
QString *qerror_human(const QError *qerror)
{
    return qerror_format_desc(qerror->error, qerror->entry);
}

/**
 * qobject_to_qerror(): Convert a QObject into a QError
 */
QError *qobject_to_qerror(const QObject *obj)
{
    if (qobject_type(obj) != QTYPE_QERROR) {
        return NULL;
    }

    return container_of(obj, QError, base);
}

/**
 * qerror_destroy_obj(): Free all memory allocated by a QError
 */
static void qerror_destroy_obj(QObject *obj)
{
    QError *qerr;

    assert(obj != NULL);
    qerr = qobject_to_qerror(obj);

    QDECREF(qerr->error);
    qemu_free(qerr);
}
