#include "libqmp.h"
#include "libqmp-internal.h"
#include "libqmp-core.h"
#include "json-streamer.h"
#include "json-parser.h"
#include "dirent.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <glib.h>

#ifndef container_of
#define offset_of(type, field) \
    ((unsigned long)(&((type *)0)->field))
#define container_of(obj, type, field) \
    ((type *)(((char *)obj) - offsetof(type, field)))
#endif

//#define DEBUG_LIBQMP 1

typedef struct FdQmpSession
{
    QmpSession session;
    JSONMessageParser parser;
    QObject *result;
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
        fs->result = NULL;
    }
}

static QDict *fd_qmp_session_read(FdQmpSession *fs)
{
    QDict *response;

    g_assert(fs->result == NULL);
    fs->result = NULL;
    while (!fs->result) {
        char buffer[1024];
        ssize_t len;

        len = read(fs->fd, buffer, sizeof(buffer));
        if (len == -1 && errno == EINTR) {
            continue;
        }
        if (len < 1) {
            abort();
        }

#if defined(DEBUG_LIBQMP)
        fwrite(buffer, len, 1, stdout);
        fflush(stdout);
#endif
        json_message_parser_feed(&fs->parser, buffer, len);
    }

    response = qobject_to_qdict(fs->result);
    fs->result = NULL;

    return response;
}

static EventTrampolineFunc *get_event_trampoline(QmpSession *sess, const char *name)
{
    QmpEventTrampoline *t;

    QTAILQ_FOREACH(t, &sess->events, node) {
        if (strcmp(t->name, name) == 0) {
            return t->dispatch;
        }
    }

    return NULL;
}

static QDict *fd_qmp_session_read_once(FdQmpSession *fs)
{
    QDict *response;

    response = fd_qmp_session_read(fs);
    if (qdict_haskey(response, "event")) {
        EventTrampolineFunc *tramp;
        QmpSignal *signal;
        const char *event;
        int tag;

        event = qdict_get_str(response, "event");
        tramp = get_event_trampoline(&fs->session, event);

        if (tramp && qdict_haskey(response, "tag")) {
            tag = qdict_get_int(response, "tag");

            QTAILQ_FOREACH(signal, &fs->session.signals, node) {
                if (signal->global_handle == tag) {
                    QmpConnection *conn;
                    QDict *args = NULL;
                    Error *err = NULL;

                    if (qdict_haskey(response, "data")) {
                        args = qobject_to_qdict(qdict_get(response, "data"));
                    }

                    QTAILQ_FOREACH(conn, &signal->connections, node) {
                        tramp(args, conn->fn, conn->opaque, &err);
                        if (err) {
                            error_free(err);
                        }
                    }

                    break;
                }
            }
        }

        QDECREF(response);
        response = NULL;
    }

    return response;
}

static QDict *fd_qmp_session_read_response(FdQmpSession *fs)
{
    QDict *response;

    do {
        response = fd_qmp_session_read_once(fs);
    } while (response == NULL);

    return response;
}

static bool qmp_session_fd_wait_event(QmpSession *s, struct timeval *tv)
{
    FdQmpSession *fs = (FdQmpSession *)s;
    fd_set readfds;
    int ret;

    FD_ZERO(&readfds);
    FD_SET(fs->fd, &readfds);

    do {
        ret = select(fs->fd + 1, &readfds, NULL, NULL, tv);
    } while (ret == -1 && errno == EINTR);

    if (ret) {
        QDict *res;
        res = fd_qmp_session_read_once(fs);
        if (res) {
            abort();
        }
        return true;
    }

    return false;
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
        if (len == -1 && errno == EINTR) {
            continue;
        }
        if (len < 1) {
            abort();
        }

#if defined(DEBUG_LIBQMP)
        fwrite(buffer + offset, size - offset, 1, stdout);
        fflush(stdout);
#endif
        offset += len;
    }

    QDECREF(str);
    QDECREF(request);

    response = fd_qmp_session_read_response(fs);

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

void libqmp_register_event(QmpSession *sess, const char *name, EventTrampolineFunc *func)
{
    QmpEventTrampoline *t = qemu_mallocz(sizeof(*t));
    t->name = name;
    t->dispatch = func;
    QTAILQ_INSERT_TAIL(&sess->events, t, node);
}

QmpSession *qmp_session_new(int fd)
{
    FdQmpSession *s = qemu_mallocz(sizeof(*s));

    s->fd = fd;
    s->session.dispatch = qmp_session_fd_dispatch;
    s->session.wait_event = qmp_session_fd_wait_event;
    s->got_greeting = false;

    QTAILQ_INIT(&s->session.events);
    QTAILQ_INIT(&s->session.signals);
    json_message_parser_init(&s->parser, fd_qmp_session_parse);

    libqmp_init_events(&s->session);
    libqmp_qmp_capabilities(&s->session, NULL);

    return &s->session;
}

void qmp_session_destroy(QmpSession *s)
{
    FdQmpSession *fs = container_of(s, FdQmpSession, session);
    if (fs->result) {
        qobject_decref(fs->result);
        fs->result = NULL;
    }
    json_message_parser_destroy(&fs->parser);
    close(fs->fd);
    qemu_free(fs);
}

static bool startswith(const char *haystack, const char *needle)
{
    int a, b;

    a = strlen(haystack);
    b = strlen(needle);

    if (a < b) {
        return false;
    }

    return memcmp(haystack, needle, b) == 0;
}

static bool endswith(const char *haystack, const char *needle)
{
    int a, b;

    a = strlen(haystack);
    b = strlen(needle);

    if (a < b) {
        return false;
    }

    return memcmp(haystack + (a - b), needle, b) == 0;
}

static int qmp_connect_name(const char *name)
{
    char *home = getenv("HOME");
    struct sockaddr_un addr;
    int s;

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path),
             "%s/.qemu/name-%s.sock", home, name);
    s = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert(s != -1);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        int ret = -errno;
        close(s);
        return ret;
    }

    return s;
}

static int qmp_connect_pid(pid_t pid)
{
    char *home = getenv("HOME");
    struct sockaddr_un addr;
    int s;

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path),
             "%s/.qemu/pid-%d.sock", home, pid);

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert(s != -1);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        int ret = -errno;
        close(s);
        return ret;
    }

    return s;
}

GuestInfo *libqmp_list_guests(void)
{
    char *home = getenv("HOME");
    struct dirent *dent;
    char path[4096];
    DIR *dir;
    GuestInfo *guest_list = NULL;

    if (home == NULL) {
        return NULL;
    }

    snprintf(path, sizeof(path), "%s/.qemu", home);
    dir = opendir(path);
    g_assert(dir != NULL);

    while ((dent = readdir(dir))) {
        char name[256];
        GuestInfo *info;

        // FIXME validate connection is still good
        if (startswith(dent->d_name, "name-") &&
            endswith(dent->d_name, ".sock")) {
            size_t len;
            int c;

            len = snprintf(name, sizeof(name), "%s", dent->d_name + 5);
            name[len - 5] = 0;

            c = qmp_connect_name(name);
            if (c < 0) {
                continue;
            }
            close(c);

            info = qmp_alloc_guest_info();
            info->has_name = true;
            info->name = qemu_strdup(name);
            info->next = guest_list;
            guest_list = info;
        } else if (startswith(dent->d_name, "pid-") &&
            endswith(dent->d_name, ".sock")) {
            size_t len;
            pid_t pid;
            int c;

            len = snprintf(name, sizeof(name), "%s", dent->d_name + 4);
            name[len - 5] = 0;

            pid = strtol(name, NULL, 10);

            c = qmp_connect_pid(pid);
            if (c < 0) {
                continue;
            }
            close(c);

            info = qmp_alloc_guest_info();
            info->has_pid = true;
            info->pid = pid;
            info->next = guest_list;
            guest_list = info;
        } else {
            continue;
        }
    }

    closedir(dir);

    return guest_list;
}

QmpSession *libqmp_session_new_name(const char *name)
{
    int c;

    c = qmp_connect_name(name);
    if (c < 0) {
        errno = -c;
        return NULL;
    }

    return qmp_session_new(c);
}

QmpSession *libqmp_session_new_pid(pid_t pid)
{
    int c;

    c = qmp_connect_pid(pid);
    if (c < 0) {
        errno = -c;
        return NULL;
    }

    return qmp_session_new(c);
}

typedef struct GenericSignal
{
    QmpSignal *signal;
} GenericSignal;

void *libqmp_signal_new(QmpSession *s, size_t size, int global_handle)
{
    GenericSignal *obj;
    obj = qemu_mallocz(size);
    obj->signal = qemu_mallocz(sizeof(QmpSignal));
    obj->signal->sess = s;
    obj->signal->global_handle = global_handle;
    // FIXME validate there isn't another global_handle
    QTAILQ_INIT(&obj->signal->connections);
    QTAILQ_INSERT_TAIL(&s->signals, obj->signal, node);
    return obj;
}

int libqmp_signal_connect(QmpSignal *obj, void *func, void *opaque)
{
    QmpConnection *conn;

    conn = qemu_mallocz(sizeof(*conn));
    conn->fn = func;
    conn->opaque = opaque;
    conn->handle = ++obj->max_handle;
    QTAILQ_INSERT_TAIL(&obj->connections, conn, node);
    return conn->handle;
}

void libqmp_signal_disconnect(QmpSignal *obj, int handle)
{
    QmpConnection *conn;

    QTAILQ_FOREACH(conn, &obj->connections, node) {
        if (conn->handle == handle) {
            break;
        }
    }
    if (conn) {
        QTAILQ_REMOVE(&obj->connections, conn, node);
        qemu_free(conn);
    }
}

void libqmp_signal_free(void *base, QmpSignal *obj)
{
    libqmp_put_event(obj->sess, obj->global_handle, NULL);
    // FIXME release all connections
    qemu_free(obj);
    qemu_free(base);
}

bool libqmp_wait_event(QmpSession *s, struct timeval *tv)
{
    return s->wait_event(s, tv);
}

bool libqmp_poll_event(QmpSession *s)
{
    struct timeval tv = { 0, 0 };
    return libqmp_wait_event(s, &tv);
}
