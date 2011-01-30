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
    int fd;
} FdQmpSession;

static void fd_qmp_session_parse(JSONMessageParser *parser, QList *tokens)
{
    FdQmpSession *fs = container_of(parser, FdQmpSession, parser);
    fs->result = json_parser_parse(tokens, NULL);
    fs->completed = true;
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

    str = qstring_from_str(name);
    QINCREF(str);
    qdict_put(request, "execute", str);
    QDECREF(str);

    if (qdict_size(args)) {
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

    return qdict_get(qobject_to_qdict(fs->result), "return");
}

QmpSession *qmp_session_new(int fd)
{
    FdQmpSession *s = qemu_mallocz(sizeof(*s));

    s->fd = fd;
    s->session.dispatch = qmp_session_fd_dispatch;

    json_message_parser_init(&s->parser, fd_qmp_session_parse);

    return &s->session;
}


