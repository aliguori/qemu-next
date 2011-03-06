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

typedef struct QmpCommand
{
    const char *name;
    bool stateful;
    QmpCommandFunc *fn;
    QmpStatefulCommandFunc *sfn;
    QTAILQ_ENTRY(QmpCommand) node;
} QmpCommand;

typedef struct DefaultQmpConnection
{
    QmpSignal *obj;
    int handle;
    QTAILQ_ENTRY(DefaultQmpConnection) node;
} DefaultQmpConnection;

struct QmpState
{
    int (*add_connection)(QmpState *s, QmpConnection *conn);
    void (*del_connection)(QmpState *s, int global_handle, Error **errp);
    void (*event)(QmpState *s, QObject *data);

    QTAILQ_HEAD(, DefaultQmpConnection) default_connections;
};

typedef struct QmpSession
{
    JSONMessageParser parser;
    QmpState state;
    CharDriverState *chr;
    int max_global_handle;
    QTAILQ_HEAD(, QmpConnection) connections;
} QmpSession;

static QTAILQ_HEAD(, QmpCommand) qmp_commands =
    QTAILQ_HEAD_INITIALIZER(qmp_commands);

void qmp_register_command(const char *name, QmpCommandFunc *fn)
{
    QmpCommand *cmd = qemu_mallocz(sizeof(*cmd));

    cmd->name = name;
    cmd->stateful = false;
    cmd->fn = fn;
    QTAILQ_INSERT_TAIL(&qmp_commands, cmd, node);
}

void qmp_register_stateful_command(const char *name, QmpStatefulCommandFunc *fn)
{
    QmpCommand *cmd = qemu_mallocz(sizeof(*cmd));

    cmd->name = name;
    cmd->stateful = true;
    cmd->sfn = fn;
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

void qmp_state_del_connection(QmpState *sess, int global_handle, Error **errp)
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

static QObject *qmp_dispatch_err(QmpState *state, QList *tokens, Error **errp)
{
    const char *command;
    QDict *args, *dict;
    QObject *request;
    QmpCommand *cmd;
    QObject *ret = NULL;
    Error *err = NULL;

    request = json_parser_parse_err(tokens, NULL, &err);
    if (request == NULL) {
        if (err == NULL) {
            error_set(errp, QERR_JSON_PARSE_ERROR, "no valid JSON object");
        } else {
            error_propagate(errp, err);
        }
        return NULL;
    }
    if (qobject_type(request) != QTYPE_QDICT) {
        error_set(errp, QERR_JSON_PARSE_ERROR, "request is not a dictionary");
        return NULL;
    }

    dict = qobject_to_qdict(request);
    if (!qdict_haskey(dict, "execute")) {
        error_set(errp, QERR_JSON_PARSE_ERROR, "no execute key");
        return NULL;
    }

    command = qdict_get_str(dict, "execute");
    cmd = qmp_find_command(command);
    if (cmd == NULL) {
        error_set(errp, QERR_COMMAND_NOT_FOUND, command);
        return NULL;
    }

    if (!qdict_haskey(dict, "arguments")) {
        args = qdict_new();
    } else {
        args = qdict_get_qdict(dict, "arguments");
        QINCREF(args);
    }

    if (cmd->stateful) {
        cmd->sfn(state, args, &ret, errp);
    } else {
        cmd->fn(args, &ret, errp);
    }

    QDECREF(args);
    qobject_decref(request);

    return ret;
}

static QObject *qmp_dispatch(QmpState *state, QList *tokens)
{
    Error *err = NULL;
    QObject *ret;
    QDict *rsp;

    ret = qmp_dispatch_err(state, tokens, &err);

    rsp = qdict_new();
    if (err) {
        qdict_put_obj(rsp, "error", error_get_qobject(err));
        error_free(err);
    } else {
        if (ret) {
            qdict_put_obj(rsp, "return", ret);
        } else {
            qdict_put(rsp, "return", qdict_new());
        }
    }

    return QOBJECT(rsp);
}

static void qmp_chr_parse(JSONMessageParser *parser, QList *tokens)
{
    QmpSession *s = container_of(parser, QmpSession, parser);
    QObject *rsp;
    QString *str;
    
    rsp = qmp_dispatch(&s->state, tokens);

    str = qobject_to_json(rsp);
    qemu_chr_write(s->chr, (void *)str->string, str->length);
    qemu_chr_write(s->chr, (void *)"\n", 1);

    QDECREF(str);
    qobject_decref(rsp);
}

static int qmp_chr_can_receive(void *opaque)
{
    return 1024;
}

static void qmp_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    QmpSession *s = opaque;
    json_message_parser_feed(&s->parser, (char *)buf, size);
}

static void qmp_chr_send_greeting(QmpSession *s)
{
    VersionInfo *info;
    QObject *vers;
    QObject *greeting;
    QString *str;

    info = qmp_query_version(NULL);
    vers = qmp_marshal_type_VersionInfo(info);
    qmp_free_version_info(info);

    greeting = qobject_from_jsonf("{'QMP': {'version': %p, 'capabilities': []} }",
                                  vers);
    str = qobject_to_json(greeting);
    qobject_decref(greeting);

    qemu_chr_write(s->chr, (void *)str->string, str->length);
    qemu_chr_write(s->chr, (void *)"\n", 1);
    QDECREF(str);
}

static void qmp_chr_event(void *opaque, int event)
{
    QmpSession *s = opaque;
    switch (event) {
    case CHR_EVENT_OPENED:
        // FIXME disconnect any connected signals including defaults
        json_message_parser_init(&s->parser, qmp_chr_parse);
        qmp_chr_send_greeting(s);
        break;
    case CHR_EVENT_CLOSED:
        json_message_parser_flush(&s->parser);
        break;
    }
}

static int qmp_chr_add_connection(QmpState *state,  QmpConnection *conn)
{
    QmpSession *s = container_of(state, QmpSession, state);

    QTAILQ_INSERT_TAIL(&s->connections, conn, node);
    return ++s->max_global_handle;
}

static void qmp_chr_send_event(QmpState *state, QObject *event)
{
    QmpSession *s = container_of(state, QmpSession, state);
    QString *str;

    str = qobject_to_json(event);
    qemu_chr_write(s->chr, (void *)str->string, str->length);
    qemu_chr_write(s->chr, (void *)"\n", 1);
    QDECREF(str);
}

static void qmp_chr_del_connection(QmpState *state, int global_handle, Error **errp)
{
    QmpSession *s = container_of(state, QmpSession, state);
    QmpConnection *conn;

    QTAILQ_FOREACH(conn, &s->connections, node) {
        if (conn->global_handle == global_handle) {
            qmp_signal_disconnect(conn->signal, conn->handle);
            QTAILQ_REMOVE(&s->connections, conn, node);
            qemu_free(conn);
            return;
        }
    }

    error_set(errp, QERR_INVALID_PARAMETER_VALUE, "tag", "valid event handle");
}

void qmp_init_chardev(CharDriverState *chr)
{
    QmpSession *s = qemu_mallocz(sizeof(*s));

    s->chr = chr;
    s->state.add_connection = qmp_chr_add_connection;
    s->state.event = qmp_chr_send_event;
    s->state.del_connection = qmp_chr_del_connection;
    QTAILQ_INIT(&s->state.default_connections);

    s->max_global_handle = 0;
    QTAILQ_INIT(&s->connections);

    qemu_chr_add_handlers(chr, qmp_chr_can_receive, qmp_chr_receive,
                          qmp_chr_event, s);
}
