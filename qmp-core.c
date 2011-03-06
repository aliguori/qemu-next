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
#include "qmp.h"
#include "qmp-core.h"
#include "json-lexer.h"
#include "json-parser.h"
#include "json-streamer.h"
#include "qemu_socket.h"
#include <glib.h>
#include "qemu-queue.h"
#include "sysemu.h"

typedef enum QmpCommandType
{
    QCT_NORMAL,
    QCT_STATEFUL,
    QCT_ASYNC,
} QmpCommandType;

typedef struct QmpCommand
{
    const char *name;
    QmpCommandType type;
    QmpCommandFunc *fn;
    QmpStatefulCommandFunc *sfn;
    QmpAsyncCommandFunc *afn;
    QTAILQ_ENTRY(QmpCommand) node;
} QmpCommand;

struct QmpCommandState
{
    QmpState *state;
    QObject *tag;
};

static QTAILQ_HEAD(, QmpCommand) qmp_commands =
    QTAILQ_HEAD_INITIALIZER(qmp_commands);

void qmp_register_command(const char *name, QmpCommandFunc *fn)
{
    QmpCommand *cmd = qemu_mallocz(sizeof(*cmd));

    cmd->name = name;
    cmd->type = QCT_NORMAL;
    cmd->fn = fn;
    QTAILQ_INSERT_TAIL(&qmp_commands, cmd, node);
}

void qmp_register_stateful_command(const char *name, QmpStatefulCommandFunc *fn)
{
    QmpCommand *cmd = qemu_mallocz(sizeof(*cmd));

    cmd->name = name;
    cmd->type = QCT_STATEFUL;
    cmd->sfn = fn;
    QTAILQ_INSERT_TAIL(&qmp_commands, cmd, node);
}

void qmp_register_async_command(const char *name, QmpAsyncCommandFunc *fn)
{
    QmpCommand *cmd = qemu_mallocz(sizeof(*cmd));

    cmd->name = name;
    cmd->type = QCT_ASYNC;
    cmd->afn = fn;
    QTAILQ_INSERT_TAIL(&qmp_commands, cmd, node);
}

static QmpCommand *qmp_find_command(const char *name)
{
    QmpCommand *i;

    QTAILQ_FOREACH(i, &qmp_commands, node) {
        if (strcmp(i->name, name) == 0) {
            return i;
        }
    }
    return NULL;
}

char *qobject_as_string(QObject *obj)
{
    char buffer[1024];

    switch (qobject_type(obj)) {
    case QTYPE_QINT:
        snprintf(buffer, sizeof(buffer), "%" PRId64,
                 qint_get_int(qobject_to_qint(obj)));
        return qemu_strdup(buffer);
    case QTYPE_QSTRING:
        return qemu_strdup(qstring_get_str(qobject_to_qstring(obj)));
    case QTYPE_QFLOAT:
        snprintf(buffer, sizeof(buffer), "%.17g",
                 qfloat_get_double(qobject_to_qfloat(obj)));
        return qemu_strdup(buffer);
    case QTYPE_QBOOL:
        if (qbool_get_int(qobject_to_qbool(obj))) {
            return qemu_strdup("on");
        }
        return qemu_strdup("off");
    default:
        return NULL;
    }
}
