#include "libqmp.h"
#include "libqmp-internal.h"
#include "libqmp-core.h"
#include "json-streamer.h"
#include "json-parser.h"

#ifndef container_of
#define offset_of(type, field) \
    ((unsigned long)(&((type *)0)->field))
#define container_of(obj, type, field) \
    ((type *)(((char *)obj) - offsetof(type, field)))
#endif

typedef struct FdQmpSession
{
    QmpSession session;
    JSONMessageParser parser;
    QObject *result;
    bool completed;
    bool got_greeting;
    int fd;
} FdQmpSession;

static void fd_qmp_session_parse(JSONMessageParser *parser, QList *tokens)
{
    FdQmpSession *fs = container_of(parser, FdQmpSession, parser);
    fs->result = json_parser_parse(tokens, NULL);
    if (!fs->got_greeting) {
        fs->got_greeting = true;
        qobject_decref(fs->result);
    } else {
        fs->completed = true;
    }
}

static QObject *qmp_session_fd_dispatch(QmpSession *s, const char *name,
                                        QDict *args, Error **err)
{
    FdQmpSession *fs = (FdQmpSession *)s;
    QString *str;
    const char *buffer;
    size_t size;
    size_t offset;
    QDict *request = qdict_new();
    QDict *response;

    qdict_put(request, "execute", qstring_from_str(name));

    if (qdict_size(args)) {
        QINCREF(args);
        qdict_put(request, "arguments", args);
    }

    str = qobject_to_json(QOBJECT(request));
    buffer = qstring_get_str(str);
    size = str->length;

    offset = 0;
    while (offset < size) {
        ssize_t len;

        len = write(fs->fd, buffer + offset, size - offset);
        offset += len;
    }

    fs->result = NULL;
    fs->completed = false;
    while (!fs->completed) {
        char buffer[1024];
        ssize_t len;

        len = read(fs->fd, buffer, sizeof(buffer));
        json_message_parser_feed(&fs->parser, buffer, len);
    }
    QDECREF(str);
    QDECREF(request);

    response = qobject_to_qdict(fs->result);
    fs->result = NULL;

    if (qdict_haskey(response, "error")) {
        error_set_qobject(err, qdict_get(response, "error"));
        QDECREF(response);
        return NULL;
    } else {
        QObject *result = qdict_get(response, "return");
        qobject_incref(result);
        QDECREF(response);
        return result;
    }
}

QmpSession *qmp_session_new(int fd)
{
    FdQmpSession *s = qemu_mallocz(sizeof(*s));

    s->fd = fd;
    s->session.dispatch = qmp_session_fd_dispatch;
    s->got_greeting = false;

    json_message_parser_init(&s->parser, fd_qmp_session_parse);

    libqmp_qmp_capabilities(&s->session, NULL);

    return &s->session;
}

void qmp_session_destroy(QmpSession *s)
{
    FdQmpSession *fs = container_of(s, FdQmpSession, session);
    close(fs->fd);
    qemu_free(fs);
}