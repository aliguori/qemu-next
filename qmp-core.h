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
#ifndef QMP_CORE_H
#define QMP_CORE_H

#include "monitor.h"
#include "qmp-marshal-types.h"
#include "error_int.h"

typedef struct QmpState QmpState;

typedef void (QmpCommandFunc)(const QDict *, QObject **, Error **);
typedef void (QmpStatefulCommandFunc)(QmpState *qmp__sess, const QDict *, QObject **, Error **);

void qmp_register_command(const char *name, QmpCommandFunc *fn);
void qmp_register_stateful_command(const char *name, QmpStatefulCommandFunc *fn);
void qmp_init_chardev(CharDriverState *chr);

char *qobject_as_string(QObject *obj);

#endif
