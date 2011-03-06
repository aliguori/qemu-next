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

CommandInfo *qmp_query_commands(Error **errp)
{
    CommandInfo *cmd_list = NULL;
    QmpCommand *cmd;

    QTAILQ_FOREACH(cmd, &qmp_commands, node) {
        CommandInfo *info = qmp_alloc_command_info();
        info->name = qemu_strdup(cmd->name);
        info->next = cmd_list;
        cmd_list = info;
    }

    return cmd_list;
}

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

static void def_signal_trampoline(QmpState *state, const char *name)
{
    QObject *event;
    qemu_timeval tv;

    qemu_gettimeofday(&tv);
    event = qobject_from_jsonf("{ 'timestamp': { 'seconds': %" PRId64 ", "
                               "                 'microseconds': %" PRId64 " }, "
                               "  'event': %s }",
                               tv.tv_sec, tv.tv_usec, name);
    state->event(state, event);
    qobject_decref(event);
}

static void shutdown_signal_trampoline(void *opaque)
{
    def_signal_trampoline(opaque, "SHUTDOWN");
}

static void reset_signal_trampoline(void *opaque)
{
    def_signal_trampoline(opaque, "RESET");
}

static void powerdown_signal_trampoline(void *opaque)
{
    def_signal_trampoline(opaque, "POWERDOWN");
}

static void stop_signal_trampoline(void *opaque)
{
    def_signal_trampoline(opaque, "STOP");
}

static void resume_signal_trampoline(void *opaque)
{
    def_signal_trampoline(opaque, "RESUME");
}

static void block_signal_trampoline(void *opaque, const char *device,
                                    const char *action, const char *operation)
{
    QmpState *state = opaque;
    QObject *event;
    qemu_timeval tv;

    qemu_gettimeofday(&tv);
    event = qobject_from_jsonf("{ 'timestamp': { 'seconds': %" PRId64 ", "
                               "                 'microseconds': %" PRId64 " }, "
                               "  'event': 'BLOCK_IO_ERROR', "
                               "  'data': { 'device': %s, "
                               "            'action': %s, "
                               "            'operation': %s } }",
                               tv.tv_sec, tv.tv_usec, device, action,
                               operation);
    state->event(state, event);
    qobject_decref(event);
}

static void qdict_putf(QDict *dict, const char *key, const char *fmt, ...)
{
    va_list ap;
    QObject *obj;

    va_start(ap, fmt);
    obj = qobject_from_jsonv(fmt, &ap);
    va_end(ap);

    qdict_put_obj(dict, key, obj);
}

static void vnc_signal_trampoline(void *opaque, const char *name,
                                  VncClientInfo *client, VncServerInfo *server)
{
    QmpState *state = opaque;
    QObject *event;
    qemu_timeval tv;
    QObject *qclient;
    QDict *dict;

    qclient = qobject_from_jsonf("{'host': %s, 'family': %s, 'service': %s}",
                                 client->host, client->family, client->service);
    dict = qobject_to_qdict(qclient);

    if (client->has_x509_dname) {
        qdict_putf(dict, "x509_dname", "%s", client->x509_dname);
    }
    if (client->has_sasl_username) {
        qdict_putf(dict, "sasl_username", "%s", client->sasl_username);
    }

    qemu_gettimeofday(&tv);
    event = qobject_from_jsonf("{ 'timestamp': { 'seconds': %" PRId64 ", "
                               "                 'microseconds': %" PRId64 " }, "
                               "  'event': %s, "
                               "  'data': { 'client': %p, "
                               "            'server': { 'host': %s, "
                               "                        'family': %s, "
                               "                        'service': %s, "
                               "                        'auth': %s } } }",
                               tv.tv_sec, tv.tv_usec, name, qclient, server->host,
                               server->family, server->service, server->auth);

    state->event(state, event);
    qobject_decref(event);
}

static void vnc_connected_trampoline(void *opaque, VncClientInfo *client,
                                     VncServerInfo *server)
{
    vnc_signal_trampoline(opaque, "VNC_CONNECTED", client, server);
}

static void vnc_initialized_trampoline(void *opaque, VncClientInfo *client,
                                     VncServerInfo *server)
{
    vnc_signal_trampoline(opaque, "VNC_INITIALIZED", client, server);
}

static void vnc_disconnected_trampoline(void *opaque, VncClientInfo *client,
                                     VncServerInfo *server)
{
    vnc_signal_trampoline(opaque, "VNC_DISCONNECTED", client, server);
}

#define full_signal_connect(state, ev, fn)                         \
do {                                                               \
    typeof(ev) event = (ev);                                       \
    DefaultQmpConnection *conn = qemu_mallocz(sizeof(*conn));      \
    conn->obj = event->signal;                                     \
    conn->handle = signal_connect(event, (fn), (state));           \
    QTAILQ_INSERT_TAIL(&(state)->default_connections, conn, node); \
} while (0)

void qmp_qmp_capabilities(QmpState *state, Error **errp)
{
    full_signal_connect(state, qmp_get_shutdown_event(NULL),
                        shutdown_signal_trampoline);
    full_signal_connect(state, qmp_get_reset_event(NULL),
                        reset_signal_trampoline);
    full_signal_connect(state, qmp_get_powerdown_event(NULL),
                        powerdown_signal_trampoline);
    full_signal_connect(state, qmp_get_stop_event(NULL),
                        stop_signal_trampoline);
    full_signal_connect(state, qmp_get_resume_event(NULL),
                        resume_signal_trampoline);
    full_signal_connect(state, qmp_get_block_io_error_event(NULL),
                        block_signal_trampoline);
    full_signal_connect(state, qmp_get_vnc_connected_event(NULL),
                        vnc_connected_trampoline);
    full_signal_connect(state, qmp_get_vnc_initialized_event(NULL),
                        vnc_initialized_trampoline);
    full_signal_connect(state, qmp_get_vnc_disconnected_event(NULL),
                        vnc_disconnected_trampoline);
}

typedef struct QmpSession
{
    JSONMessageParser parser;
    QmpState state;
    CharDriverState *chr;
    int max_global_handle;
    QTAILQ_HEAD(, QmpConnection) connections;
} QmpSession;

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

struct QmpUnixServer
{
    int fd;
    char path[4096];
};

typedef struct QmpUnixSession
{
    int fd;
    GString *tx;
    bool notify_write;
    QmpState state;
    JSONMessageParser parser;
    QTAILQ_HEAD(, QmpConnection) connections;
    int max_global_handle;
} QmpUnixSession;

static void qmp_unix_session_try_write(QmpUnixSession *sess);

static void qmp_unix_session_delete(QmpUnixSession *sess)
{
    while (!QTAILQ_EMPTY(&sess->state.default_connections)) {
        DefaultQmpConnection *conn;

        conn = QTAILQ_FIRST(&sess->state.default_connections);
        qmp_signal_disconnect(conn->obj, conn->handle);
        QTAILQ_REMOVE(&sess->state.default_connections, conn, node);
        qemu_free(conn);
    }

    while (!QTAILQ_EMPTY(&sess->connections)) {
        QmpConnection *conn;

        conn = QTAILQ_FIRST(&sess->connections);
        qmp_signal_disconnect(conn->signal, conn->handle);
        // FIXME free global_handle
        QTAILQ_REMOVE(&sess->connections, conn, node);
        qemu_free(conn);
    }

    qemu_set_fd_handler(sess->fd, NULL, NULL, NULL);
    close(sess->fd);
    g_string_free(sess->tx, TRUE);
    qemu_free(sess);
}

static void qmp_unix_session_read_event(void *opaque)
{
    QmpUnixSession *sess = opaque;
    char buffer[1024];
    ssize_t len;

    do {
        len = read(sess->fd, buffer, sizeof(buffer));
    } while (len == -1 && errno == EINTR);

    if (len == -1 && errno == EAGAIN) {
        return;
    }

    if (len < 1) {
        qmp_unix_session_delete(sess);
        return;
    }

    json_message_parser_feed(&sess->parser, buffer, len);
}

static void qmp_unix_session_write_event(void *opaque)
{
    QmpUnixSession *sess = opaque;
    qmp_unix_session_try_write(sess);
}

static void qmp_unix_session_update_handlers(QmpUnixSession *sess)
{
    if (sess->notify_write) {
        qemu_set_fd_handler(sess->fd, qmp_unix_session_read_event,
                            qmp_unix_session_write_event, sess);
    } else {
        qemu_set_fd_handler(sess->fd, qmp_unix_session_read_event,
                            NULL, sess);
    }
}

static void qmp_unix_session_try_write(QmpUnixSession *sess)
{
    ssize_t len;

    sess->notify_write = false;
    qmp_unix_session_update_handlers(sess);

    if (sess->tx->len == 0) {
        return;
    }
    
    do {
        len = write(sess->fd, sess->tx->str, sess->tx->len);
    } while (len == -1 && errno == EINTR);

    if (len == -1 && errno == EAGAIN) {
        sess->notify_write = true;
        qmp_unix_session_update_handlers(sess);
        return;
    }

    if (len < 1) {
        qmp_unix_session_delete(sess);
        return;
    }

    memmove(sess->tx->str, sess->tx->str + len, sess->tx->len - len);
    g_string_truncate(sess->tx, sess->tx->len - len);
}

static void qmp_unix_session_send(QmpUnixSession *sess, const QObject *obj)
{
    QString *str;

    str = qobject_to_json(obj);
    g_string_append(sess->tx, qstring_get_str(str));
    g_string_append(sess->tx, "\n");
    QDECREF(str);

    qmp_unix_session_try_write(sess);
}

static void qmp_unix_session_send_greeting(QmpUnixSession *sess)
{
    QObject *vers, *greeting;
    VersionInfo *info;

    info = qmp_query_version(NULL);
    vers = qmp_marshal_type_VersionInfo(info);
    qmp_free_version_info(info);

    greeting = qobject_from_jsonf("{'QMP': {'version': %p, 'capabilities': []} }",
                                  vers);
    qmp_unix_session_send(sess, greeting);
    qobject_decref(greeting);
}

static void qmp_unix_session_parse(JSONMessageParser *parser, QList *tokens)
{
    QmpUnixSession *sess = container_of(parser, QmpUnixSession, parser);
    QObject *rsp;

    rsp = qmp_dispatch(&sess->state, tokens);
    qmp_unix_session_send(sess, rsp);
    qobject_decref(rsp);
}

static int qmp_unix_session_add_connection(QmpState *state,  QmpConnection *conn)
{
    QmpUnixSession *sess = container_of(state, QmpUnixSession, state);

    QTAILQ_INSERT_TAIL(&sess->connections, conn, node);
    return ++sess->max_global_handle;
}

static void qmp_unix_session_del_connection(QmpState *state, int global_handle, Error **errp)
{
    QmpUnixSession *sess = container_of(state, QmpUnixSession, state);
    QmpConnection *conn;

    QTAILQ_FOREACH(conn, &sess->connections, node) {
        if (conn->global_handle == global_handle) {
            qmp_signal_disconnect(conn->signal, conn->handle);
            QTAILQ_REMOVE(&sess->connections, conn, node);
            qemu_free(conn);
            return;
        }
    }

    error_set(errp, QERR_INVALID_PARAMETER_VALUE, "tag", "valid event handle");
}

static void qmp_unix_session_event(QmpState *state, QObject *event)
{
    QmpUnixSession *sess = container_of(state, QmpUnixSession, state);
    qmp_unix_session_send(sess, event);
}

static void qmp_unix_server_read_event(void *opaque)
{
    QmpUnixServer *s = opaque;
    QmpUnixSession *sess = qemu_mallocz(sizeof(*sess));
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);

    sess->fd = accept(s->fd, (struct sockaddr *)&addr, &addrlen);
    sess->tx = g_string_new("");
    sess->notify_write = false;
    sess->state.add_connection = qmp_unix_session_add_connection;
    sess->state.del_connection = qmp_unix_session_del_connection;
    sess->state.event = qmp_unix_session_event;
    QTAILQ_INIT(&sess->state.default_connections);
    QTAILQ_INIT(&sess->connections);

    json_message_parser_init(&sess->parser, qmp_unix_session_parse);
    qmp_unix_session_update_handlers(sess);

    qmp_unix_session_send_greeting(sess);
}

void qmp_unix_server_delete(QmpUnixServer *srv)
{
    // FIXME delete all sessions
    close(srv->fd);
    unlink(srv->path);
    qemu_free(srv);
}

/**
 * There is some magic here to enforce uniqueness.  With a unix domain socket,
 * bind() has the nice property that it fails if the name exists.  This means
 * you can treat bind() as an atomic operation to ensure that you don't have
 * two instances squash each other.
 *
 * Unfortunately, the file name sticks around even after the associated file
 * descriptor goes away.  That means that if a QEMU instance crashes, there may
 * be an orphan socket name floating around.
 *
 * Normally, the way to handle this is to just unlink() before binding but
 * this creates both a race condition and makes it possible for one QEMU
 * instance to squash another.
 *
 * The solution is as follows:
 *
 *  1) Try to bind() to the path
 *
 *  2) If the address is in use, determine if there is still a server running
 *     by attempting to connect to it.
 *
 *  3) If noone is listening on the socket, open a file and take an advisory
 *     lock on the file.
 *
 *  4) While holding the lock, unlink and bind() to the path name.  Once bound,
 *     release the lock.
 *
 * This still races.  Perhaps we should hold the lock file before ever
 * attempting to bind.
 */
QmpUnixServer *qmp_unix_server_new(const char *path)
{
    QmpUnixServer *s = qemu_mallocz(sizeof(*s));
    struct sockaddr_un addr;
    int ret;

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    snprintf(s->path, sizeof(s->path), "%s", path);

    s->fd = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert(s->fd != -1);

    ret = bind(s->fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1 && errno == EADDRINUSE) {
        int c;

        c = socket(PF_UNIX, SOCK_STREAM, 0);
        g_assert(c != -1);

        ret = connect(c, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == 0) {
            close(s->fd);
            close(c);
            qemu_free(s);
            errno = EADDRINUSE;
            return NULL;
        }
        if (ret == -1 && errno == ECONNREFUSED) {
            char lock_path[4096];
            int fd;

            snprintf(lock_path, sizeof(lock_path), "%s.lock", path);

            fd = open(lock_path, O_RDWR | O_CREAT, 0644);
            g_assert(fd != -1);

            ret = lockf(fd, F_TLOCK, 0);
            g_assert(ret != -1);
            
            unlink(path);
            ret = bind(s->fd, (struct sockaddr *)&addr, sizeof(addr));

            close(fd);
            unlink(lock_path);
        } else {
            printf("unknown error: %m\n");
        }
        close(c);
    }
    g_assert(ret != -1);

    ret = listen(s->fd, 1);
    g_assert(ret != -1);

    socket_set_nonblock(s->fd);
    qemu_set_fd_handler(s->fd, qmp_unix_server_read_event, NULL, s);
    return s;
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

