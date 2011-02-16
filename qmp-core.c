#include "qmp.h"
#include "qmp-core.h"
#include "json-lexer.h"
#include "json-parser.h"
#include "json-streamer.h"
#include "qemu_socket.h"
#include <glib.h>

typedef struct QmpCommand
{
    const char *name;
    QmpCommandFunc *fn;
    QTAILQ_ENTRY(QmpCommand) node;
} QmpCommand;

static QTAILQ_HEAD(, QmpCommand) qmp_commands =
    QTAILQ_HEAD_INITIALIZER(qmp_commands);

void qmp_register_command(const char *name, QmpCommandFunc *fn)
{
    QmpCommand *cmd = qemu_mallocz(sizeof(*cmd));

    cmd->name = name;
    cmd->fn = fn;
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

typedef struct QmpSession
{
    JSONMessageParser parser;
    CharDriverState *chr;
} QmpSession;

static void qmp_chr_parse(JSONMessageParser *parser, QList *tokens)
{
    QmpSession *s = container_of(parser, QmpSession, parser);
    QObject *request, *ret = NULL;
    QString *str;
    QDict *dict, *args;
    QmpCommand *cmd;
    Error *err = NULL;
    QDict *rsp;

    request = json_parser_parse(tokens, NULL);
    str = qobject_to_json_pretty(request);
    QDECREF(str);

    if (qobject_type(request) != QTYPE_QDICT) {
        return;
    }
    dict = qobject_to_qdict(request);
    if (!qdict_haskey(dict, "execute")) {
        return;
    }

    cmd = qmp_find_command(qdict_get_str(dict, "execute"));
    if (cmd == NULL) {
        return;
    }

    if (!qdict_haskey(dict, "arguments")) {
        args = qdict_new();
    } else {
        args = qdict_get_qdict(dict, "arguments");
    }

    cmd->fn(args, &ret, &err);
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

    str = qobject_to_json(QOBJECT(rsp));
    qemu_chr_write(s->chr, (void *)str->string, str->length);
    qemu_chr_write(s->chr, (void *)"\n", 1);

    QDECREF(str);
    QDECREF(rsp);
    qobject_decref(request);
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
        json_message_parser_init(&s->parser, qmp_chr_parse);
        qmp_chr_send_greeting(s);
        break;
    case CHR_EVENT_CLOSED:
        json_message_parser_flush(&s->parser);
        break;
    }
}

void qmp_init_chardev(CharDriverState *chr)
{
    QmpSession *s = qemu_mallocz(sizeof(*s));

    s->chr = chr;

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
    JSONMessageParser parser;
} QmpUnixSession;

static void qmp_unix_session_try_write(QmpUnixSession *sess);

static void qmp_unix_session_delete(QmpUnixSession *sess)
{
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
    QObject *request, *ret = NULL;
    QDict *dict, *args;
    QmpCommand *cmd;
    Error *err = NULL;
    QDict *rsp;

    request = json_parser_parse(tokens, NULL);
    if (qobject_type(request) != QTYPE_QDICT) {
        return;
    }

    dict = qobject_to_qdict(request);
    if (!qdict_haskey(dict, "execute")) {
        return;
    }

    cmd = qmp_find_command(qdict_get_str(dict, "execute"));
    if (cmd == NULL) {
        return;
    }

    if (!qdict_haskey(dict, "arguments")) {
        args = qdict_new();
    } else {
        args = qdict_get_qdict(dict, "arguments");
    }

    cmd->fn(args, &ret, &err);

    qobject_decref(request);
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

    qmp_unix_session_send(sess, QOBJECT(rsp));

    QDECREF(rsp);
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
