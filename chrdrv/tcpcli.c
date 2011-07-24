#include "qemu/tcpcli.h"

void tcp_client_initialize(TcpClient *obj, const char *id)
{
    type_initialize(obj, TYPE_TCP_CLIENT, id);
}

void tcp_client_finalize(TcpClient *obj)
{
    type_finalize(obj);
}

const char *tcp_client_get_host(TcpClient *obj, Error **errp)
{
    return obj->host;
}

void tcp_client_set_host(TcpClient *obj, const char *value, Error **errp)
{
    qemu_free(obj->host);
    obj->host = qemu_strdup(value);
}

const char *tcp_client_get_port(TcpClient *obj, Error **errp)
{
    return obj->port;
}

void tcp_client_set_port(TcpClient *obj, const char *value, Error **errp)
{
    qemu_free(obj->port);
    obj->port = qemu_strdup(value);
}

bool tcp_client_get_ipv4(TcpClient *obj, Error **errp)
{
    return obj->ipv4;
}

void tcp_client_set_ipv4(TcpClient *obj, bool value, Error **errp)
{
    obj->ipv4 = value;
}

bool tcp_client_get_ipv6(TcpClient *obj, Error **errp)
{
    return obj->ipv6;
}

void tcp_client_set_ipv6(TcpClient *obj, bool value, Error **errp)
{
    obj->ipv6 = value;
}

static void tcp_client_init(TypeInstance *inst)
{
    TcpClient *s = TCP_CLIENT(inst);

    plug_add_property_str(PLUG(s), "host",
                          (PlugPropertyGetterStr *)tcp_client_get_host,
                          (PlugPropertySetterStr *)tcp_client_set_host,
                          PROP_F_READWRITE);

    plug_add_property_str(PLUG(s), "port",
                          (PlugPropertyGetterStr *)tcp_client_get_port,
                          (PlugPropertySetterStr *)tcp_client_set_port,
                          PROP_F_READWRITE);

    plug_add_property_bool(PLUG(s), "ipv4",
                           (PlugPropertyGetterBool *)tcp_client_get_ipv4,
                           (PlugPropertySetterBool *)tcp_client_set_ipv4,
                           PROP_F_READWRITE);

    plug_add_property_bool(PLUG(s), "ipv6",
                           (PlugPropertyGetterBool *)tcp_client_get_ipv6,
                           (PlugPropertySetterBool *)tcp_client_set_ipv6,
                           PROP_F_READWRITE);
}

static void tcp_client_open(CharDriver *chr, Error **errp)
{
    /* FIXME create connection */
}

static void tcp_client_class_init(TypeClass *class)
{
    CharDriverClass *cdc = CHAR_DRIVER_CLASS(class);

    cdc->open = tcp_client_open;
}

static TypeInfo tcp_client_info = {
    .name = TYPE_TCP_CLIENT,
    .parent = TYPE_SOCKET_CHAR_DRIVER,
    .instance_size = sizeof(TcpClient),
    .class_init = tcp_client_class_init,
    .instance_init = tcp_client_init,
};

static void register_backends(void)
{
    type_register_static(&tcp_client_info);
}

device_init(register_backends);
