#include "qmp.h"
#include "qmp-core.h"
#include "json-lexer.h"
#include "json-parser.h"
#include "json-streamer.h"

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
    qmp_free_VersionInfo(info);

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