/*
 * virt-proxy - host/guest communication layer
 *
 * Copyright IBM Corp. 2010
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "virtproxy.h"

#define VP_SERVICE_ID_LEN 32    /* max length of service id string */
#define VP_PKT_DATA_LEN 1024    /* max proxied bytes per VPPacket */
#define VP_CONN_DATA_LEN 1024   /* max bytes conns can send at a time */
#define VP_MAGIC 0x1F374059

/* listening fd, one for each service we're forwarding to remote end */
typedef struct VPOForward {
    int listen_fd;
    char service_id[VP_SERVICE_ID_LEN];
    QLIST_ENTRY(VPOForward) next;
} VPOForward;

/* proxied client/server connected states */
typedef struct VPConn {
    int client_fd;
    int server_fd;
    enum {
        VP_CONN_CLIENT = 1,
        VP_CONN_SERVER,
    } type;
    enum {
        VP_STATE_NEW = 1,   /* accept()'d and registered fd */
        VP_STATE_INIT,      /* sent init pkt to remote end, waiting for ack */
        VP_STATE_CONNECTED, /* client and server connected */
    } state;
    QLIST_ENTRY(VPConn) next;
} VPConn;

typedef struct {
    enum {
        VP_CONTROL_CONNECT_INIT = 1,
        VP_CONTROL_CONNECT_ACK,
        VP_CONTROL_CLOSE,
    } type;
    union {
        /* tell remote end connect to server and map client_fd to it */
        struct {
            int client_fd;
            char service_id[VP_SERVICE_ID_LEN];
        } connect_init;
        /* tell remote end we've created the connection to the server,
         * and give them the corresponding fd to use so we don't have
         * to do a reverse lookup everytime
         */
        struct {
            int client_fd;
            int server_fd;
        } connect_ack;
        /* tell remote end to close fd in question, presumably because
         * connection was closed on our end
         */
        struct {
            int client_fd;
            int server_fd;
        } close;
    } args;
} VPControlMsg;

typedef struct {
    enum {
        VP_PKT_CONTROL = 1,
        VP_PKT_CLIENT,
        VP_PKT_SERVER,
    } type;
    union {
        VPControlMsg msg;
        struct {
            int client_fd;
            int server_fd;
            int bytes;
            char data[VP_PKT_DATA_LEN];
        } proxied;
    } payload;
    int magic;
} __attribute__((__packed__)) VPPacket;
