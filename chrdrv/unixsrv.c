#include "qemu/unixsrv.h"

void unix_server_initialize(UnixServer *obj, const char *id)
{
    type_initialize(obj, TYPE_UNIX_SERVER, id);
}

void unix_server_finalize(UnixServer *obj)
{
    type_finalize(obj);
}

const char *unix_server_get_path(UnixServer *obj, Error **errp)
{
    return obj->path;
}

void unix_server_set_path(UnixServer *obj, const char *path, Error **errp)
{
    qemu_free(obj->path);
    obj->path = qemu_strdup(path);

    socket_server_rebind(SOCKET_SERVER(obj));
}

static int unix_server_make_listen_socket(SocketServer *ss)
{
    UnixServer *s = UNIX_SERVER(ss);
    struct sockaddr_un addr;
    int ret;
    int fd = -1;

    fd = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        return -1;
    }

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", s->path);

    ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1) {
        closesocket(fd);
        return -1;
    }

    listen(fd, 1);

    return fd;
}

static int unix_server_accept(SocketServer *ss)
{
    int fd;

    do {
        struct sockaddr_un addr;
        socklen_t addrlen = sizeof(addr);
        fd = qemu_accept(ss->listen_fd, (struct sockaddr *)&addr, &addrlen);
    } while (fd == -1 && errno == EINTR);

    return fd;
}

static void unix_server_init(TypeInstance *inst)
{
    UnixServer *s = UNIX_SERVER(inst);

    plug_add_property_str(PLUG(s), "path",
                          (PlugPropertyGetterStr *)unix_server_get_path,
                          (PlugPropertySetterStr *)unix_server_set_path,
                          PROP_F_READWRITE);
}

static void unix_server_fini(TypeInstance *inst)
{
    UnixServer *s = UNIX_SERVER(inst);

    qemu_free(s->path);
}

static void unix_server_class_init(TypeClass *class)
{
    SocketServerClass *ssc = SOCKET_SERVER_CLASS(class);

    ssc->accept = unix_server_accept;
    ssc->make_listen_socket = unix_server_make_listen_socket;
}

static TypeInfo unix_server_type_info = {
    .name = TYPE_UNIX_SERVER,
    .parent = TYPE_SOCKET_SERVER,
    .instance_size = sizeof(UnixServer),
    .instance_init = unix_server_init,
    .instance_finalize = unix_server_fini,
    .class_init = unix_server_class_init,
};

static void register_backends(void)
{
    type_register_static(&unix_server_type_info);
}

device_init(register_backends);
