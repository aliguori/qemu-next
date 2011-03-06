/*
 * QAPI
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */
#include "qmp-marshal-types-core.h"
#include "qerror.h"

QObject *qmp_marshal_type_int(int64_t value)
{
    return QOBJECT(qint_from_int(value));
}

QObject *qmp_marshal_type_str(const char *value)
{
    return QOBJECT(qstring_from_str(value));
}

QObject *qmp_marshal_type_bool(bool value)
{
    return QOBJECT(qbool_from_int(value));
}

QObject *qmp_marshal_type_number(double value)
{
    return QOBJECT(qfloat_from_double(value));
}

int64_t qmp_unmarshal_type_int(QObject *value, Error **errp)
{
    if (qobject_type(value) != QTYPE_QINT) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, "<unknown>", "int");
        return 0;
    }
    return qint_get_int(qobject_to_qint(value));
}

char *qmp_unmarshal_type_str(QObject *value, Error **errp)
{
    if (qobject_type(value) != QTYPE_QSTRING) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, "<unknown>", "string");
        return 0;
    }
    return qemu_strdup(qstring_get_str(qobject_to_qstring(value)));
}

bool qmp_unmarshal_type_bool(QObject *value, Error **errp)
{
    if (qobject_type(value) != QTYPE_QBOOL) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, "<unknown>", "bool");
        return 0;
    }
    return qbool_get_int(qobject_to_qbool(value));
}

double qmp_unmarshal_type_number(QObject *value, Error **errp)
{
    if (qobject_type(value) != QTYPE_QFLOAT) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, "<unknown>", "float");
        return 0;
    }
    return qfloat_get_double(qobject_to_qfloat(value));
}


