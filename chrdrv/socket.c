#include "socket.h"
#include "qemu_socket.h"

void socket_chr_initialize(SocketCharDriver *obj, const char *id)
{
    type_initialize(obj, TYPE_SOCKET_CHAR_DRIVER, id);
}

void socket_chr_finalize(SocketCharDriver *obj)
{
    type_finalize(obj);
}

static int socket_chr_write(CharDriver *chr, const uint8_t *buf, int len)
{
    SocketCharDriver *s = SOCKET_CHAR_DRIVER(chr);

    if (!s->connected) {
        return len;
    }

    return send_all(s->fd, buf, len);
}
    
static int socket_chr_get_msgfd(CharDriver *chr)
{
    SocketCharDriver *s = SOCKET_CHAR_DRIVER(chr);
    int fd = s->msgfd;
    s->msgfd = -1;
    return fd;
}

#ifndef _WIN32
static void unix_process_msgfd(CharDriver *chr, struct msghdr *msg)
{
    SocketCharDriver *s = SOCKET_CHAR_DRIVER(chr);
    struct cmsghdr *cmsg;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        int fd;

        if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ||
            cmsg->cmsg_level != SOL_SOCKET ||
            cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }

        fd = *((int *)CMSG_DATA(cmsg));
        if (fd < 0) {
            continue;
        }

        if (s->msgfd != -1) {
            close(s->msgfd);
        }
        s->msgfd = fd;
    }
}

static ssize_t socket_chr_recv(CharDriver *chr, char *buf, size_t len)
{
    SocketCharDriver *s = SOCKET_CHAR_DRIVER(chr);
    struct msghdr msg = { NULL, };
    struct iovec iov[1];
    union {
        struct cmsghdr cmsg;
        char control[CMSG_SPACE(sizeof(int))];
    } msg_control;
    ssize_t ret;

    iov[0].iov_base = buf;
    iov[0].iov_len = len;

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = &msg_control;
    msg.msg_controllen = sizeof(msg_control);

    ret = recvmsg(s->fd, &msg, 0);
    if (ret > 0) {
        unix_process_msgfd(chr, &msg);
    }

    return ret;
}
#else
static ssize_t socket_chr_recv(CharDriver *chr, char *buf, size_t len)
{
    SocketCharDriver *s = SOCKET_CHAR_DRIVER(chr);
    return recv(s->fd, buf, len, 0);
}
#endif

#define READ_BUF_LEN 1024

static void socket_chr_try_read(void *opaque)
{
    SocketCharDriver *s = opaque;
    uint8_t buf[READ_BUF_LEN];
    int len, size;

    /* Hopefully no drivers rely on can_read anymore.. */
    s->max_size = char_driver_can_read(CHAR_DRIVER(s));
    if (s->max_size <= 0) {
        return;
    }

    len = sizeof(buf);
    if (len > s->max_size) {
        len = s->max_size;
    }
    size = socket_chr_recv(CHAR_DRIVER(s), (void *)buf, len);
    if (size == 0) {
        /* connection closed */
        s->connected = 0;
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
        s->fd = -1;
        char_driver_event(CHAR_DRIVER(s), CHR_EVENT_CLOSED);
        // fixme call something that socketserver can hook
    } else if (size > 0) {
        char_driver_read(CHAR_DRIVER(s), buf, size);
    }
}

void socket_chr_connect(SocketCharDriver *s)
{
    s->connected = 1;
    qemu_set_fd_handler(s->fd, socket_chr_try_read, NULL, s);
// FIXME    qemu_chr_generic_open(chr);
}

static void socket_chr_close(CharDriver *chr)
{
    SocketCharDriver *s = SOCKET_CHAR_DRIVER(chr);

    if (s->fd >= 0) {
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
    }
    char_driver_event(chr, CHR_EVENT_CLOSED);
}

static void socket_chr_init(TypeInstance *inst)
{
    SocketCharDriver *s = SOCKET_CHAR_DRIVER(inst);

    s->fd = -1;
}

static void socket_chr_class_init(TypeClass *class)
{
    CharDriverClass *cdc = CHAR_DRIVER_CLASS(class);

    cdc->write = socket_chr_write;
    cdc->close = socket_chr_close;
    cdc->get_msgfd = socket_chr_get_msgfd;
}

static TypeInfo socket_chr_info = {
    .name = TYPE_SOCKET_CHAR_DRIVER,
    .parent = TYPE_CHAR_DRIVER,
    .instance_size = sizeof(SocketCharDriver),
    .class_init = socket_chr_class_init,
    .instance_init = socket_chr_init,
};

/* Socket Server */

void socket_server_initialize(SocketServer *obj, const char *id)
{
    type_initialize(obj, TYPE_SOCKET_SERVER, id);
}

void socket_server_finalize(SocketServer *obj)
{
    type_finalize(obj);
}

static void socket_server_accept(void *opaque)
{
    SocketServer *s = SOCKET_SERVER(opaque);
    SocketCharDriver *scd = SOCKET_CHAR_DRIVER(s);
    SocketServerClass *ssc = SOCKET_SERVER_GET_CLASS(s);
    int fd;

    fd = ssc->accept(s);
    socket_set_nonblock(fd);
    scd->fd = fd;
    qemu_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
    socket_chr_connect(scd);
}

static void socket_server_open(CharDriver *chr, Error **errp)
{
    SocketServer *s = SOCKET_SERVER(chr);

    socket_server_rebind(s);
}

bool socket_server_get_wait(SocketServer *s)
{
    return s->wait;
}

void socket_server_set_wait(SocketServer *s, bool value)
{
    s->wait = value;
}

const char *socket_server_get_peername(SocketServer *s)
{
    SocketServerClass *ssc = SOCKET_SERVER_GET_CLASS(s);

    g_assert(ssc->get_peername != NULL);

    return ssc->get_peername(s);
}

void socket_server_rebind(SocketServer *s)
{
    SocketServerClass *ssc = SOCKET_SERVER_GET_CLASS(s);

    /* Don't immediately rebind if the backend isn't realized */
    if (!plug_get_realized(PLUG(s))) {
        return;
    }

    if (s->listen_fd != -1) {
        qemu_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
        close(s->listen_fd);
    }

    s->listen_fd = ssc->make_listen_socket(s);
    if (!s->wait) {
        socket_set_nonblock(s->listen_fd);
        qemu_set_fd_handler(s->listen_fd, socket_server_accept, NULL, s);
    } else {
        socket_server_accept(s);
    }
}

static void socket_server_init(TypeInstance *inst)
{
    SocketServer *s = SOCKET_SERVER(inst);

    s->listen_fd = -1;

    plug_add_property_bool(PLUG(s), "wait",
                           (PlugPropertyGetterBool *)socket_server_get_wait,
                           (PlugPropertySetterBool *)socket_server_set_wait,
                           PROP_F_READWRITE);

    plug_add_property_str(PLUG(s), "peername",
                          (PlugPropertyGetterStr *)socket_server_get_peername,
                          NULL,
                          PROP_F_READ);
}

static void socket_server_class_init(TypeClass *class)
{
    CharDriverClass *cdc = CHAR_DRIVER_CLASS(class);
    cdc->open = socket_server_open;
}

static TypeInfo socket_server_info = {
    .name = TYPE_SOCKET_SERVER,
    .parent = TYPE_SOCKET_CHAR_DRIVER,
    .instance_size = sizeof(SocketServer),
    .class_size = sizeof(SocketServerClass),
    .class_init = socket_server_class_init,
    .instance_init = socket_server_init,
};

static void register_backends(void)
{
    type_register_static(&socket_chr_info);
    type_register_static(&socket_server_info);
}

device_init(register_backends);
