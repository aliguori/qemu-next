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

struct QmpState
{
    int (*add_connection)(QmpState *s, QmpConnection *conn);
    void (*del_connection)(QmpState *s, int global_handle, Error **errp);
    void (*event)(QmpState *s, QObject *data);
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

void qmp_state_add_connection(QmpState *sess, const char *event_name, QmpSignal *obj, int handle, QmpConnection *conn)
{
    conn->state = sess;
    conn->event_name = event_name;
    conn->signal = obj;
    conn->handle = handle;
    conn->global_handle = sess->add_connection(sess, conn);
}

void qmp_put_event(QmpState *sess, int global_handle, Error **errp)
{
    sess->del_connection(sess, global_handle, errp);
}

void qmp_state_event(QmpConnection *conn, QObject *data)
{
    QDict *event = qdict_new();
    qemu_timeval tv;
    QObject *ts;

    qemu_gettimeofday(&tv);

    ts = qobject_from_jsonf("{ 'seconds': %" PRId64 ", "
                            "'microseconds': %" PRId64 " }",
                            (int64_t)tv.tv_sec, (int64_t)tv.tv_usec);
    qdict_put_obj(event, "timestamp", ts);

    qdict_put(event, "event", qstring_from_str(conn->event_name));
    if (data) {
        qobject_incref(data);
        qdict_put_obj(event, "data", data);
    }

    qdict_put(event, "tag", qint_from_int(conn->global_handle));

    conn->state->event(conn->state, QOBJECT(event));
    QDECREF(event);
}

QmpSignal *qmp_signal_init(void)
{
    QmpSignal *obj = qemu_mallocz(sizeof(*obj));
    obj->max_handle = 0;
    obj->ref = 1;
    QTAILQ_INIT(&obj->slots);
    return obj;
}

void qmp_signal_ref(QmpSignal *obj)
{
    obj->ref++;
}

void qmp_signal_unref(QmpSignal *obj)
{
    if (--obj->ref) {
        qemu_free(obj);
    }
}

int qmp_signal_connect(QmpSignal *obj, void *func, void *opaque)
{
    int handle = ++obj->max_handle;
    QmpSlot *slot = qemu_mallocz(sizeof(*slot));

    slot->handle = handle;
    slot->func = func;
    slot->opaque = opaque;

    QTAILQ_INSERT_TAIL(&obj->slots, slot, node);

    return handle;
}

void qmp_signal_disconnect(QmpSignal *obj, int handle)
{
    QmpSlot *slot;

    QTAILQ_FOREACH(slot, &obj->slots, node) {
        if (slot->handle == handle) {
            QTAILQ_REMOVE(&obj->slots, slot, node);
            qemu_free(slot);
            break;
        }
    }
}
