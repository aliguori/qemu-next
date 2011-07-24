#ifndef CHAR_DRIVER_UNIX_H
#define CHAR_DRIVER_UNIX_H

#include "qemu/socketchr.h"
#include "qemu_socket.h"

typedef struct UnixServer
{
    SocketServer parent;

    /* private */
    char *path;
} UnixServer;

#define TYPE_UNIX_SERVER "unix-server"
#define UNIX_SERVER(obj) TYPE_CHECK(UnixServer, obj, TYPE_UNIX_SERVER)

void unix_server_initialize(UnixServer *obj, const char *id);
void unix_server_finalize(UnixServer *obj);

const char *unix_server_get_path(UnixServer *obj, Error **errp);
void unix_server_set_path(UnixServer *obj, const char *value, Error **errp);

#endif
