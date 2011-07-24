#ifndef CHR_TCP_CLIENT_H
#define CHR_TCP_CLIENT_H

#include "qemu/socketchr.h"

typedef struct TcpClient
{
    SocketCharDriver parent;
    char *host;
    char *port;
    bool ipv4;
    bool ipv6;
} TcpClient;

#define TYPE_TCP_CLIENT "tcp-client"
#define TCP_CLIENT(obj) TYPE_CHECK(TcpClient, obj, TYPE_TCP_CLIENT)

void tcp_client_initialize(TcpClient *obj, const char *id);
void tcp_client_finalize(TcpClient *obj);

const char *tcp_client_get_host(TcpClient *obj, Error **errp);
void tcp_client_set_host(TcpClient *obj, const char *value, Error **errp);

const char *tcp_client_get_port(TcpClient *obj, Error **errp);
void tcp_client_set_port(TcpClient *obj, const char *value, Error **errp);

bool tcp_client_get_ipv4(TcpClient *obj, Error **errp);
void tcp_client_set_ipv4(TcpClient *obj, bool value, Error **errp);

bool tcp_client_get_ipv6(TcpClient *obj, Error **errp);
void tcp_client_set_ipv6(TcpClient *obj, bool value, Error **errp);

#endif
