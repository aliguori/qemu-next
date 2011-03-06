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
#ifndef QMP_MARSHAL_TYPES_CORE_H
#define QMP_MARSHAL_TYPES_CORE_H

#include "qemu-common.h"
#include "qemu-objects.h"
#include "qerror.h"
#include "error.h"
#include "qmp-types.h"

QObject *qmp_marshal_type_int(int64_t value);
QObject *qmp_marshal_type_str(const char *value);
QObject *qmp_marshal_type_bool(bool value);
QObject *qmp_marshal_type_number(double value);

int64_t qmp_unmarshal_type_int(QObject *value, Error **errp);
char *qmp_unmarshal_type_str(QObject *value, Error **errp);
bool qmp_unmarshal_type_bool(QObject *value, Error **errp);
double qmp_unmarshal_type_number(QObject *value, Error **errp);

#endif
