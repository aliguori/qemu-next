#ifndef CHAR_DRIVER_TCP_H
#define CHAR_DRIVER_TCP_H

#include "qemu/socketchr.h"
#include "qemu_socket.h"

typedef struct TcpServer
{
    SocketServer parent;

    struct sockaddr_in peer;

    char *host;
    char *port;

    bool ipv4;
    bool ipv6;
} TcpServer;

#define TYPE_TCP_SERVER "tcp-server"
#define TCP_SERVER(obj) TYPE_CHECK(TcpServer, obj, TYPE_TCP_SERVER)

void tcp_server_initialize(TcpServer *obj, const char *id);
void tcp_server_finalize(TcpServer *obj);

const char *tcp_server_get_host(TcpServer *obj, Error **errp);
void tcp_server_set_host(TcpServer *obj, const char *value, Error **errp);

const char *tcp_server_get_port(TcpServer *obj, Error **errp);
void tcp_server_set_port(TcpServer *obj, const char *value, Error **errp);

bool tcp_server_get_ipv4(TcpServer *obj, Error **errp);
void tcp_server_set_ipv4(TcpServer *obj, bool value, Error **errp);

bool tcp_server_get_ipv6(TcpServer *obj, Error **errp);
void tcp_server_set_ipv6(TcpServer *obj, bool value, Error **errp);

const char *tcp_server_get_peername(TcpServer *obj, Error **errp);

#endif
