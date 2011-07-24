#ifndef CHR_SOCKET_H
#define CHR_SOCKET_H

#include "qemu/chrdrv.h"

/**
 * @SocketCharDriver:
 *
 * Base class for any @CharDriver that uses a socket as a transport.
 */
typedef struct SocketCharDriver
{
    CharDriver parent;

    /* Private */

    /**
     * @connected whether the transport is connected
     */
    int connected;

    /**
     * @fd the data socket fd
     */
    int fd;

    /**
     * @msgfd the received file descriptor from the transport
     */
    int msgfd;
} SocketCharDriver;

typedef struct SocketCharDriverClass
{
    CharDriverClass parent_class;
} SocketCharDriverClass;

#define TYPE_SOCKET_CHAR_DRIVER "socket-char-driver"
#define SOCKET_CHAR_DRIVER(obj) \
    TYPE_CHECK(SocketCharDriver, obj, TYPE_SOCKET_CHAR_DRIVER)

void socket_chr_connect(SocketCharDriver *s);

/**
 * @SocketServer:
 *
 * The base class for any socket based transport that accepts connections.
 */
typedef struct SocketServer
{
    SocketCharDriver parent;

    /* protected */

    /**
     * @wait if true, then immediate block until a client connects
     */
    bool wait;

    /**
     * @listen_fd the fd of the socket to accept connections on
     */
    int listen_fd;
} SocketServer;

typedef struct SocketServerClass
{
    SocketCharDriverClass parent_class;

    /* Protected */

    /**
     * @make_listen_socket
     *
     * This function should create a socket and bind it such that its
     * suitable for listening.
     */
    int (*make_listen_socket)(SocketServer *obj);

    /**
     * @accept
     *
     * This function should accept a connection on @listen_fd and return
     * a socket representing that connection.
     */
    int (*accept)(SocketServer *obj);
} SocketServerClass;

#define TYPE_SOCKET_SERVER "socket-server"
#define SOCKET_SERVER(obj) \
    TYPE_CHECK(SocketServer, obj, TYPE_SOCKET_SERVER)
#define SOCKET_SERVER_CLASS(class) \
    TYPE_CLASS_CHECK(SocketServerClass, class, TYPE_SOCKET_SERVER)
#define SOCKET_SERVER_GET_CLASS(obj) \
    TYPE_GET_CLASS(SocketServerClass, obj, TYPE_SOCKET_SERVER)

bool socket_server_get_wait(SocketServer *s, Error **errp);
void socket_server_set_wait(SocketServer *s, bool value, Error **errp);

/**
 * @socket_server_rebind:
 *
 * Rebinds the server by closing and reopening the listen_fd.  This is only
 * meant to be used by sub classes implementations.
 */
void socket_server_rebind(SocketServer *s);

#endif

