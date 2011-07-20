#ifndef CHR_SOCKET_H
#define CHR_SOCKET_H

#include "chrdrv.h"

typedef struct SocketCharDriver
{
    CharDriver parent;

    int max_size;
    int connected;
    int fd;
    int msgfd;
} SocketCharDriver;

typedef struct SocketCharDriverClass
{
    CharDriverClass parent_class;
} SocketCharDriverClass;

#define TYPE_SOCKET_CHAR_DRIVER "socket-char-driver"
#define SOCKET_CHAR_DRIVER(obj) TYPE_CHECK(SocketCharDriver, obj, TYPE_SOCKET_CHAR_DRIVER)

void socket_chr_initialize(SocketCharDriver *obj, const char *id);
void socket_chr_finalize(SocketCharDriver *obj);
void socket_chr_connect(SocketCharDriver *s);

typedef struct SocketServer
{
    SocketCharDriver parent;

    /* public */
    bool wait;
    int listen_fd;
} SocketServer;

typedef struct SocketServerClass
{
    SocketCharDriverClass parent_class;

    /* Public */
    const char *(*get_peername)(SocketServer *obj);

    /* Protected */
    int (*make_listen_socket)(SocketServer *obj);
    int (*accept)(SocketServer *obj);
} SocketServerClass;

#define TYPE_SOCKET_SERVER "socket-server"
#define SOCKET_SERVER(obj) \
    TYPE_CHECK(SocketServer, obj, TYPE_SOCKET_SERVER)
#define SOCKET_SERVER_CLASS(class) \
    TYPE_CLASS_CHECK(SocketServerClass, class, TYPE_SOCKET_SERVER)
#define SOCKET_SERVER_GET_CLASS(obj) \
    TYPE_GET_CLASS(SocketServerClass, obj, TYPE_SOCKET_SERVER)

void socket_server_initialize(SocketServer *obj, const char *id);
void socket_server_finalize(SocketServer *obj);

bool socket_server_get_wait(SocketServer *s);
void socket_server_set_wait(SocketServer *s, bool value);

const char *socket_server_get_peername(SocketServer *s);

void socket_server_rebind(SocketServer *s);

#endif

