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
#include "qemu_socket.h"

#define DEBUG_VP

#ifdef DEBUG_VP
#define TRACE(msg, ...) do { \
    fprintf(stderr, "%s:%s():L%d: " msg "\n", \
            __FILE__, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
} while(0)
#else
#define TRACE(msg, ...) \
    do { } while (0)
#endif

#define LOG(msg, ...) do { \
    fprintf(stderr, "%s:%s(): " msg "\n", \
            __FILE__, __FUNCTION__, ## __VA_ARGS__); \
} while(0)

#define VP_SERVICE_ID_LEN 32    /* max length of service id string */
#define VP_PKT_DATA_LEN 1024    /* max proxied bytes per VPPacket */
#define VP_CONN_DATA_LEN 1024   /* max bytes conns can send at a time */
#define VP_CHAN_DATA_LEN 4096   /* max bytes channel can send at a time */
#define VP_MAGIC 0x1F374059

/* listening fd, one for each service we're forwarding to remote end */
typedef struct VPOForward {
    VPDriver *drv;
    int listen_fd;
    char service_id[VP_SERVICE_ID_LEN];
    QLIST_ENTRY(VPOForward) next;
} VPOForward;

/* service_id->path/port mapping of each service forwarded from remote end */
typedef struct VPIForward {
    VPDriver *drv;
    char service_id[VP_SERVICE_ID_LEN];
    QemuOpts *socket_opts;
    QLIST_ENTRY(VPIForward) next;
} VPIForward;

/* proxied client/server connected states */
typedef struct VPConn {
    VPDriver *drv;
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

struct VPDriver {
    int channel_fd;
    int listen_fd;
    char buf[sizeof(VPPacket)];
    int buflen;
    QLIST_HEAD(, VPOForward) oforwards;
    QLIST_HEAD(, VPIForward) iforwards;
    QLIST_HEAD(, VPConn) conns;
};

static QemuOptsList vp_socket_opts = {
    .name = "vp_socket_opts",
    .head = QTAILQ_HEAD_INITIALIZER(vp_socket_opts.head),
    .desc = {
        {
            .name = "path",
            .type = QEMU_OPT_STRING,
        },{
            .name = "host",
            .type = QEMU_OPT_STRING,
        },{
            .name = "port",
            .type = QEMU_OPT_STRING,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },
        { /* end if list */ }
    },
};

static void vp_channel_read(void *opaque);

/* get VPConn by fd, "client" denotes whether to look for client or server */
static VPConn *get_conn(const VPDriver *drv, int fd, bool client)
{
    VPConn *c = NULL;

    if (client) {
        QLIST_FOREACH(c, &drv->conns, next) {
            if (c->client_fd == fd) {
                return c;
            }
        }
    } else {
        QLIST_FOREACH(c, &drv->conns, next) {
            if (c->server_fd == fd) {
                return c;
            }
        }
    }

    return NULL;
}

static void vp_channel_accept(void *opaque);

/* get VPOForward by service_id */
static VPOForward *get_oforward(const VPDriver *drv, const char *service_id)
{
    VPOForward *f = NULL;

    QLIST_FOREACH(f, &drv->oforwards, next) {
        if (strncmp(f->service_id, service_id, VP_SERVICE_ID_LEN) == 0) {
            return f;
        }
    }

    return NULL;
}

/* get VPIForward by service_id */
static VPIForward *get_iforward(const VPDriver *drv, const char *service_id)
{
    VPIForward *f = NULL;

    QLIST_FOREACH(f, &drv->iforwards, next) {
        if (strncmp(f->service_id, service_id, VP_SERVICE_ID_LEN) == 0) {
            return f;
        }
    }

    return NULL;
}

/* accept handler for communication channel
 *
 * accept()s connection to communication channel (for sockets), and sets
 * up the read handler for resulting FD.
 */
static void vp_channel_accept(void *opaque)
{
    VPDriver *drv = opaque;

    TRACE("called with opaque: %p", drv);

    struct sockaddr_in saddr;
    struct sockaddr *addr;
    socklen_t len;
    int fd;

    TRACE("called with opaque: %p", drv);

    for(;;) {
        len = sizeof(saddr);
        addr = (struct sockaddr *)&saddr;
        fd = qemu_accept(drv->listen_fd, addr, &len);

        if (fd < 0 && errno != EINTR) {
            TRACE("accept() failed");
            return;
        } else if (fd >= 0) {
            TRACE("accepted connection");
            break;
        }
    }

    drv->channel_fd = fd;
    vp_set_fd_handler(drv->channel_fd, vp_channel_read, NULL, drv);
    /* dont accept anymore connections until channel_fd is closed */
    vp_set_fd_handler(drv->listen_fd, NULL, NULL, NULL);
}

/* handle control packets
 *
 * process VPPackets containing control messages
 */
static int vp_handle_control_packet(VPDriver *drv, const VPPacket *pkt)
{
    const VPControlMsg *msg = &pkt->payload.msg;
    int ret;

    TRACE("called with drv: %p", drv);

    switch (msg->type) {
    case VP_CONTROL_CONNECT_INIT: {
        int client_fd = msg->args.connect_init.client_fd;
        int server_fd;
        char service_id[VP_SERVICE_ID_LEN];
        VPPacket resp_pkt;
        VPConn *new_conn;
        VPIForward *iforward;

        pstrcpy(service_id, VP_SERVICE_ID_LEN,
                 msg->args.connect_init.service_id);
        TRACE("setting up connection for service id %s", service_id);

        /* create server connection on behalf of remote end */
        iforward = get_iforward(drv, service_id);
        if (iforward == NULL) {
            LOG("no forwarder configured for service id");
            return -1;
        }

        qemu_opts_print(iforward->socket_opts, NULL);
        if (qemu_opt_get(iforward->socket_opts, "host") != NULL) {
            server_fd = inet_connect_opts(iforward->socket_opts);
        } else if (qemu_opt_get(iforward->socket_opts, "path") != NULL) {
            server_fd = unix_connect_opts(iforward->socket_opts);
        } else {
            LOG("unable to find listening socket host/addr info");
            return -1;
        }

        if (server_fd == -1) {
            LOG("failed to create connection to service with id %s",
                service_id);
        }
        TRACE("server_fd: %d", server_fd);

        new_conn = qemu_mallocz(sizeof(VPConn));
        if (!new_conn) {
            LOG("memory allocation failed");
            return -1;
        }

        /* send a connect_ack back over the channel */
        /* TODO: all fields should be explicitly set so we shouldn't
         * need to memset. this might hurt if we beef up VPPacket size
         */
        memset(&resp_pkt, 0, sizeof(resp_pkt));
        resp_pkt.type = VP_PKT_CONTROL;
        resp_pkt.payload.msg.type = VP_CONTROL_CONNECT_ACK;
        resp_pkt.payload.msg.args.connect_ack.server_fd = server_fd;
        resp_pkt.payload.msg.args.connect_ack.client_fd = client_fd;
        resp_pkt.magic = VP_MAGIC;

        /* TODO: can this potentially block or cause a deadlock with
         * the remote end? need to look into potentially buffering these
         * if it looks like the remote end is waiting for us to read data
         * off the channel.
         */
        if (drv->channel_fd == -1) {
            TRACE("channel no longer connected, ignoring packet");
            return -1;
        }

        ret = vp_send_all(drv->channel_fd, (void *)&resp_pkt, sizeof(resp_pkt));
        if (ret == -1) {
            LOG("error sending data over channel");
            return -1;
        }
        if (ret != sizeof(resp_pkt)) {
            TRACE("buffer full? %d bytes remaining", ret);
            return -1;
        }

        /* add new VPConn to list and set a read handler for it */
        new_conn->drv = drv;
        new_conn->client_fd = client_fd;
        new_conn->server_fd = server_fd;
        new_conn->type = VP_CONN_SERVER;
        new_conn->state = VP_STATE_CONNECTED;
        QLIST_INSERT_HEAD(&drv->conns, new_conn, next);
        vp_set_fd_handler(server_fd, vp_conn_read, NULL, new_conn);

        break;
    }
    case VP_CONTROL_CONNECT_ACK: {
        int client_fd = msg->args.connect_ack.client_fd;
        int server_fd = msg->args.connect_ack.server_fd;
        VPConn *conn;

        TRACE("recieved ack from remote end for client fd %d", client_fd);

        if (server_fd <= 0) {
            LOG("remote end sent invalid server fd");
            return -1;
        }

        conn = get_conn(drv, client_fd, true);

        if (conn == NULL) {
            LOG("failed to find connection with client_fd %d", client_fd);
            return -1;
        }

        conn->server_fd = server_fd;
        conn->state = VP_STATE_CONNECTED;
        vp_set_fd_handler(client_fd, vp_conn_read, NULL, conn);

        break;
    }
    case VP_CONTROL_CLOSE: {
        int fd;
        VPConn *conn;

        TRACE("closing connection on behalf of remote end");

        if (msg->args.close.client_fd >= 0) {
            fd = msg->args.close.client_fd;
            TRACE("recieved close msg from remote end for client fd %d", fd);
            conn = get_conn(drv, fd, true);
        } else if (msg->args.close.server_fd >= 0) {
            fd = msg->args.close.server_fd;
            TRACE("recieved close msg from remote end for server fd %d", fd);
            conn = get_conn(drv, fd, false);
        } else {
            LOG("invalid fd");
            return -1;
        }

        if (conn == NULL) {
            LOG("failed to find conn with specified fd %d", fd);
            return -1;
        }

        closesocket(fd);
        vp_set_fd_handler(fd, NULL, NULL, conn);
        QLIST_REMOVE(conn, next);
        qemu_free(conn);
        break;
    }
    }
    return 0;
}

/* handle data packets
 *
 * process VPPackets containing data and send them to the corresponding
 * FDs
 */
static int vp_handle_data_packet(void *drv, const VPPacket *pkt)
{
    int fd, ret;

    TRACE("called with drv: %p", drv);

    switch (pkt->type) {
    case VP_PKT_CLIENT:
        TRACE("recieved client packet, client fd: %d, server fd: %d",
              pkt->payload.proxied.client_fd, pkt->payload.proxied.server_fd);
        fd = pkt->payload.proxied.server_fd;
        /* TODO: proxied in non-blocking mode can causes us to spin here
         * for slow servers/clients. need to use write()'s and maintain
         * a per-conn write queue that we clear out before sending any
         * more data to the fd
         */
        TRACE("payload.proxied.bytes: %d", pkt->payload.proxied.bytes);
        ret = vp_send_all(fd, (void *)pkt->payload.proxied.data,
                pkt->payload.proxied.bytes);
        if (ret == -1) {
            LOG("error sending data over channel");
            return -1;
        }
        if (ret != pkt->payload.proxied.bytes) {
            TRACE("buffer full?");
            return -1;
        }
        break;
    case VP_PKT_SERVER:
        TRACE("recieved client packet, client fd: %d, server fd: %d",
              pkt->payload.proxied.client_fd, pkt->payload.proxied.server_fd);
        fd = pkt->payload.proxied.client_fd;
        ret = vp_send_all(fd, (void *)pkt->payload.proxied.data,
                pkt->payload.proxied.bytes);
        if (ret == -1) {
            LOG("error sending data over channel");
            return -1;
        }
        if (ret != pkt->payload.proxied.bytes) {
            TRACE("buffer full?");
            return -1;
        }
        break;
    default:
        TRACE("unknown packet type");
    }
    return 0;
}

/* read handler for communication channel
 *
 * de-multiplexes data coming in over the channel. for control messages
 * we process them here, for data destined for a service or client we
 * send it to the appropriate FD.
 */
static void vp_channel_read(void *opaque)
{
    VPDriver *drv = opaque;
    VPPacket pkt;
    int count, ret, buf_offset;
    char buf[VP_CHAN_DATA_LEN];
    char *pkt_ptr, *buf_ptr;

    TRACE("called with opaque: %p", drv);

    count = read(drv->channel_fd, buf, sizeof(buf));

    if (count == -1) {
        LOG("read() failed: %s", strerror(errno));
        return;
    } else if (count == 0) {
        /* TODO: channel closed, this probably shouldn't happen for guest-side
         * serial/virtio-serial connections, but need to confirm and consider
         * what should happen in this case. as it stands this virtproxy instance
         * is basically defunct at this point, same goes for "client" instances
         * of virtproxy where the remote end has hung-up.
         */
        LOG("channel connection closed");
        vp_set_fd_handler(drv->channel_fd, NULL, NULL, drv);
        drv->channel_fd = -1;
        if (drv->listen_fd) {
            vp_set_fd_handler(drv->listen_fd, vp_channel_accept, NULL, drv);
        }
        /* TODO: should close/remove/delete all existing VPConns here */
    }

    if (drv->buflen + count >= sizeof(VPPacket)) {
        TRACE("initial packet, drv->buflen: %d", drv->buflen);
        pkt_ptr = (char *)&pkt;
        memcpy(pkt_ptr, drv->buf, drv->buflen);
        pkt_ptr += drv->buflen;
        memcpy(pkt_ptr, buf, sizeof(VPPacket) - drv->buflen);
        /* handle first packet */
        ret = vp_handle_packet(drv, &pkt);
        if (ret != 0) {
            LOG("error handling packet");
        }
        /* handle the rest of the buffer */
        buf_offset = sizeof(VPPacket) - drv->buflen;
        drv->buflen = 0;
        buf_ptr = buf + buf_offset;
        count -= buf_offset;
        while (count > 0) {
            if (count >= sizeof(VPPacket)) {
                /* handle full packet */
                TRACE("additional packet, drv->buflen: %d", drv->buflen);
                memcpy((void *)&pkt, buf_ptr, sizeof(VPPacket));
                ret = vp_handle_packet(drv, &pkt);
                if (ret != 0) {
                    LOG("error handling packet");
                }
                count -= sizeof(VPPacket);
                buf_ptr += sizeof(VPPacket);
            } else {
                /* buffer the remainder */
                TRACE("buffering packet");
                memcpy(drv->buf, buf_ptr, count);
                drv->buflen = count;
                break;
            }
        }
    } else {
        /* haven't got a full VPPacket yet, buffer for later */
        buf_ptr = drv->buf + drv->buflen;
        memcpy(buf_ptr, buf, count);
        drv->buflen += count;
    }
}

/* handler to accept() and init new client connections */
static void vp_oforward_accept(void *opaque)
{
    VPOForward *f = opaque;
    VPDriver *drv = f->drv;

    struct sockaddr_in saddr;
    struct sockaddr *addr;
    socklen_t len;
    int fd, ret;
    VPConn *conn = NULL;
    VPPacket pkt;
    VPControlMsg msg;

    TRACE("called with opaque: %p, drv: %p", f, drv);

    for(;;) {
        len = sizeof(saddr);
        addr = (struct sockaddr *)&saddr;
        fd = qemu_accept(f->listen_fd, addr, &len);

        if (fd < 0 && errno != EINTR) {
            TRACE("accept() failed");
            return;
        } else if (fd >= 0) {
            TRACE("accepted connection");
            break;
        }
    }

    if (drv->channel_fd == -1) {
        TRACE("communication channel not open, closing connection");
        closesocket(fd);
        return;
    }

    /* send init packet over channel */
    memset(&msg, 0, sizeof(VPControlMsg));
    msg.type = VP_CONTROL_CONNECT_INIT;
    msg.args.connect_init.client_fd = fd;
    pstrcpy(msg.args.connect_init.service_id, VP_SERVICE_ID_LEN, f->service_id);

    memset(&pkt, 0, sizeof(VPPacket));
    pkt.type = VP_PKT_CONTROL;
    pkt.payload.msg = msg;
    pkt.magic = VP_MAGIC;

    ret = vp_send_all(drv->channel_fd, &pkt, sizeof(VPPacket));
    if (ret == -1) {
        LOG("vp_send_all() failed");
        return;
    }

    /* create new VPConn for client */
    conn = qemu_mallocz(sizeof(VPConn));
    conn->drv = drv;
    conn->client_fd = fd;
    conn->type = VP_CONN_CLIENT;
    conn->state = VP_STATE_NEW;
    QLIST_INSERT_HEAD(&drv->conns, conn, next);

    socket_set_nonblock(fd);
}

/* create/init VPDriver object */
VPDriver *vp_new(int fd, bool listen)
{
    VPDriver *drv = NULL;

    drv = qemu_mallocz(sizeof(VPDriver));
    drv->listen_fd = -1;
    drv->channel_fd = -1;
    QLIST_INIT(&drv->oforwards);
    QLIST_INIT(&drv->conns);

    if (listen) {
        /* provided FD is to be listened on for channel connection */
        drv->listen_fd = fd;
        vp_set_fd_handler(drv->listen_fd, vp_channel_accept, NULL, drv);
    } else {
        drv->channel_fd = fd;
        vp_set_fd_handler(drv->channel_fd, vp_channel_read, NULL, drv);
    }

    return drv;
}

/* set/modify/remove a service_id -> net/unix listening socket mapping
 *
 * "service_id" is a user-defined id for the service. this is what the
 * client end will tag it's connections with so that the remote end can
 * route it to the proper socket on the remote end.
 *
 * "fd" is a listen()'ing socket we want virtproxy to listen for new
 * connections of this service type on. set "fd" to -1 to remove the 
 * existing listening socket for this "service_id"
 */
int vp_set_oforward(VPDriver *drv, int fd, const char *service_id)
{
    VPOForward *f = get_oforward(drv, service_id);

    if (fd == -1) {
        if (f != NULL) {
            vp_set_fd_handler(f->listen_fd, NULL, NULL, NULL);
            QLIST_REMOVE(f, next);
            qemu_free(f);
        }
        return 0;
    }

    if (f == NULL) {
        f = qemu_mallocz(sizeof(VPOForward));
        f->drv = drv;
        strncpy(f->service_id, service_id, VP_SERVICE_ID_LEN);
        QLIST_INSERT_HEAD(&drv->oforwards, f, next);
    } else {
        closesocket(f->listen_fd);
    }

    f->listen_fd = fd;
    vp_set_fd_handler(f->listen_fd, vp_oforward_accept, NULL, f);

    return 0;
}
