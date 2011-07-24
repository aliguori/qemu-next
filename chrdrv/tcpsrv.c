#include "qemu/tcpsrv.h"

void tcp_server_initialize(TcpServer *obj, const char *id)
{
    type_initialize(obj, TYPE_TCP_SERVER, id);
}

void tcp_server_finalize(TcpServer *obj)
{
    type_finalize(obj);
}

const char *tcp_server_get_host(TcpServer *obj, Error **errp)
{
    return obj->host;
}

void tcp_server_set_host(TcpServer *obj, const char *value, Error **errp)
{
    qemu_free(obj->host);
    obj->host = qemu_strdup(value);

    socket_server_rebind(SOCKET_SERVER(obj));
}

const char *tcp_server_get_peername(TcpServer *obj, Error **errp)
{
    /* FIXME */
    return "w00t!";
}

const char *tcp_server_get_port(TcpServer *obj, Error **errp)
{
    return obj->port;
}

void tcp_server_set_port(TcpServer *obj, const char *value, Error **errp)
{
    qemu_free(obj->port);
    obj->port = qemu_strdup(value);

    socket_server_rebind(SOCKET_SERVER(obj));
}

bool tcp_server_get_ipv4(TcpServer *obj, Error **errp)
{
    return obj->ipv4;
}

void tcp_server_set_ipv4(TcpServer *obj, bool value, Error **errp)
{
    obj->ipv4 = value;
    socket_server_rebind(SOCKET_SERVER(obj));
}

bool tcp_server_get_ipv6(TcpServer *obj, Error **errp)
{
    return obj->ipv6;
}

void tcp_server_set_ipv6(TcpServer *obj, bool value, Error **errp)
{
    obj->ipv6 = value;
    socket_server_rebind(SOCKET_SERVER(obj));
}

static int tcp_server_make_listen_socket(SocketServer *ss)
{
    TcpServer *s = TCP_SERVER(ss);
    struct addrinfo ai, *res, *e;
    int ret;
    int fd = -1;

    memset(&ai, 0, sizeof(ai));
    ai.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    ai.ai_family = PF_UNSPEC;
    ai.ai_socktype = SOCK_STREAM;

    if (s->ipv4) {
        ai.ai_family = PF_INET;
    }

    if (s->ipv6) {
        ai.ai_family = PF_INET6;
    }

    ret = getaddrinfo(s->host, s->port, &ai, &res);
    if (ret != 0) {
        return -ret;
    }

    for (e = res; e != NULL; e = e->ai_next) {
        char uaddr[INET6_ADDRSTRLEN + 1];
        char uport[32 + 1];
        int on = 1;
        int off = 0;

        getnameinfo((struct sockaddr *)e->ai_addr, e->ai_addrlen,
                    uaddr, INET6_ADDRSTRLEN,
                    uport, sizeof(uport) - 1,
                    NI_NUMERICHOST | NI_NUMERICSERV);

        fd = qemu_socket(e->ai_family, e->ai_socktype, e->ai_protocol);
        if (fd == -1) {
            continue;
        }

        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef IPV6_V6ONLY
        if (e->ai_family == PF_INET6) {
            /* listen on both ipv4 and ipv6 */
            setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
        }
#endif

        ret = bind(fd, e->ai_addr, e->ai_addrlen);
        if (ret == 0) {
            break;
        }

        closesocket(fd);
        fd = -1;
    }

    listen(fd, 1);

    return fd;
}

static int tcp_server_accept(SocketServer *ss)
{
    TcpServer *s = TCP_SERVER(ss);
    int fd;

    do {
        socklen_t addrlen = sizeof(s->peer);
        fd = qemu_accept(ss->listen_fd, (struct sockaddr *)&s->peer, &addrlen);
    } while (fd == -1 && errno == EINTR);

    return fd;
}

static void tcp_server_init(TypeInstance *inst)
{
    TcpServer *s = TCP_SERVER(inst);

    plug_add_property_str(PLUG(s), "host",
                          (PlugPropertyGetterStr *)tcp_server_get_host,
                          (PlugPropertySetterStr *)tcp_server_set_host,
                          PROP_F_READWRITE);

    plug_add_property_str(PLUG(s), "port",
                          (PlugPropertyGetterStr *)tcp_server_get_port,
                          (PlugPropertySetterStr *)tcp_server_set_port,
                          PROP_F_READWRITE);

    plug_add_property_bool(PLUG(s), "ipv4",
                           (PlugPropertyGetterBool *)tcp_server_get_ipv4,
                           (PlugPropertySetterBool *)tcp_server_set_ipv4,
                           PROP_F_READWRITE);

    plug_add_property_bool(PLUG(s), "ipv6",
                           (PlugPropertyGetterBool *)tcp_server_get_ipv6,
                           (PlugPropertySetterBool *)tcp_server_set_ipv6,
                           PROP_F_READWRITE);

    plug_add_property_str(PLUG(s), "peername",
                          (PlugPropertyGetterStr *)tcp_server_get_peername,
                          NULL,
                          PROP_F_READ);
}

static void tcp_server_fini(TypeInstance *inst)
{
    TcpServer *s = TCP_SERVER(inst);

    qemu_free(s->port);
    qemu_free(s->host);
}

static void tcp_server_class_init(TypeClass *class)
{
    SocketServerClass *ssc = SOCKET_SERVER_CLASS(class);

    ssc->accept = tcp_server_accept;
    ssc->make_listen_socket = tcp_server_make_listen_socket;
}

static TypeInfo tcp_server_type_info = {
    .name = TYPE_TCP_SERVER,
    .parent = TYPE_SOCKET_SERVER,
    .instance_size = sizeof(TcpServer),
    .instance_init = tcp_server_init,
    .instance_finalize = tcp_server_fini,
    .class_init = tcp_server_class_init,
};

static void register_backends(void)
{
    type_register_static(&tcp_server_type_info);
}

device_init(register_backends);
