/*
 * virt-proxy - host/guest communication daemon
 *
 * Copyright IBM Corp. 2010
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <getopt.h>
#include <err.h>
#include "qemu-option.h"
#include "qemu_socket.h"
#include "virtproxy.h"

static bool verbose_enabled = 0;
#define DEBUG_ENABLED

#ifdef DEBUG_ENABLED
#define DEBUG(msg, ...) do { \
    fprintf(stderr, "%s:%s():L%d: " msg "\n", \
            __FILE__, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
} while(0)
#else
#define DEBUG(msg, ...) do {} while (0)
#endif

#define INFO(msg, ...) do { \
    if (!verbose_enabled) { \
        break; \
    } \
    warnx(msg, ## __VA_ARGS__); \
} while(0)

/* mirror qemu I/O-related code for standalone daemon */
typedef struct IOHandlerRecord {
    int fd;
    IOCanReadHandler *fd_read_poll;
    IOHandler *fd_read;
    IOHandler *fd_write;
    int deleted;
    void *opaque;
    /* temporary data */
    struct pollfd *ufd;
    QLIST_ENTRY(IOHandlerRecord) next;
} IOHandlerRecord;

static QLIST_HEAD(, IOHandlerRecord) io_handlers =
    QLIST_HEAD_INITIALIZER(io_handlers);

int vp_set_fd_handler2(int fd,
                         IOCanReadHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    IOHandlerRecord *ioh;

    if (!fd_read && !fd_write) {
        QLIST_FOREACH(ioh, &io_handlers, next) {
            if (ioh->fd == fd) {
                ioh->deleted = 1;
                break;
            }
        }
    } else {
        QLIST_FOREACH(ioh, &io_handlers, next) {
            if (ioh->fd == fd)
                goto found;
        }
        ioh = qemu_mallocz(sizeof(IOHandlerRecord));
        QLIST_INSERT_HEAD(&io_handlers, ioh, next);
    found:
        ioh->fd = fd;
        ioh->fd_read_poll = fd_read_poll;
        ioh->fd_read = fd_read;
        ioh->fd_write = fd_write;
        ioh->opaque = opaque;
        ioh->deleted = 0;
    }
    return 0;
}

int vp_set_fd_handler(int fd,
                        IOHandler *fd_read,
                        IOHandler *fd_write,
                        void *opaque)
{
    return vp_set_fd_handler2(fd, NULL, fd_read, fd_write, opaque);
}

int vp_send_all(int fd, const void *buf, int len1)
{
    int ret, len;

    len = len1;
    while (len > 0) {
        ret = write(fd, buf, len);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                warn("write() failed");
                return -1;
            }
        } else if (ret == 0) {
            break;
        } else {
            buf += ret;
            len -= ret;
        }
    }
    return len1 - len;
}

static void main_loop_wait(int nonblocking)
{
    IOHandlerRecord *ioh;
    fd_set rfds, wfds, xfds;
    int ret, nfds;
    struct timeval tv;
    int timeout = 100000;

    if (nonblocking) {
        timeout = 0;
    }

    /* poll any events */
    nfds = -1;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);
    QLIST_FOREACH(ioh, &io_handlers, next) {
        if (ioh->deleted)
            continue;
        if (ioh->fd_read &&
            (!ioh->fd_read_poll ||
             ioh->fd_read_poll(ioh->opaque) != 0)) {
            FD_SET(ioh->fd, &rfds);
            if (ioh->fd > nfds)
                nfds = ioh->fd;
        }
        if (ioh->fd_write) {
            FD_SET(ioh->fd, &wfds);
            if (ioh->fd > nfds)
                nfds = ioh->fd;
        }
    }

    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    ret = select(nfds + 1, &rfds, &wfds, &xfds, &tv);

    if (ret > 0) {
        IOHandlerRecord *pioh;

        QLIST_FOREACH_SAFE(ioh, &io_handlers, next, pioh) {
            if (ioh->deleted) {
                QLIST_REMOVE(ioh, next);
                qemu_free(ioh);
                continue;
            }
            if (ioh->fd_read && FD_ISSET(ioh->fd, &rfds)) {
                ioh->fd_read(ioh->opaque);
            }
            if (ioh->fd_write && FD_ISSET(ioh->fd, &wfds)) {
                ioh->fd_write(ioh->opaque);
            }
        }
    }
}

#define VP_ARG_LEN 256

static QemuOptsList vp_opts = {
    .name = "vpargs",
    .head = QTAILQ_HEAD_INITIALIZER(vp_opts.head),
    .desc = {
        {
            .name = "service_id",
            .type = QEMU_OPT_STRING,
        },{
            .name = "channel_method",
            .type = QEMU_OPT_STRING,
        },{
            .name = "index",
            .type = QEMU_OPT_NUMBER,
        },{
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

typedef struct VPData {
    QemuOpts *opts;
    void *opaque;
    QTAILQ_ENTRY(VPData) next;
} VPData;

static QTAILQ_HEAD(, VPData) iforwards;
static QTAILQ_HEAD(, VPData) oforwards;
static QTAILQ_HEAD(, VPData) channels;

static void usage(const char *cmd)
{
    printf(
"Usage: %s -c <channel_opts> [-c ... ] [-i <iforward_opts> ...] "
"[-o <oforward_opts> ...]\n"
"QEMU virt-proxy communication channel\n"
"\n"
"  -c, --channel    channel options of the form:\n"
"                   <method>:<addr>:<port>[:channel_id]\n"
"  -o, --oforward   oforward options of the form:\n"
"                   <service_id>:<addr>:<port>[:channel_id]\n"
"  -i, --iforward   iforward options of the form:\n"
"                   <service_id>:<addr>:<port>[:channel_id]\n"
"  -v, --verbose    display extra debugging information\n"
"  -h, --help       display this help and exit\n"
"\n"
"  channels are used to establish a data connection between 2 end-points in\n"
"  the host or the guest (connection method specified by <method>).\n"
"  oforwards specify a socket to listen for new connections on, outgoing\n"
"  data from which is tagged with <service_id> before being sent over the\n"
"  channel. iforwards specify a socket to route incoming data/connections\n"
"  with a specific <service_id> to. The positional parameters for\n"
"  channels/iforwards/oforwards are:\n"
"\n"
"  <method>:     one of unix-connect, unix-listen, tcp-connect, tcp-listen,\n"
"                virtserial-open\n"
"  <addr>:       path of unix socket or virtserial port, or IP of host, to\n"
"                connect/bind to\n"
"  <port>:       port to bind/connect to, or '-' if addr is a path\n"
"  <service_id>: an identifier used to properly route connections to the\n"
"                corresponding host or guest daemon socket.\n"
"  <channel_id>: numerical id to identify what channel to use for an iforward\n"
"                or oforward. (default is 0)\n"
"\n"
"Report bugs to <mdroth@linux.vnet.ibm.com>\n"
    , cmd);
}

static int vp_parse(QemuOpts *opts, const char *str, bool is_channel)
{
    /* TODO: use VP_SERVICE_ID_LEN, bring it into virtproxy.h */
    char service_id[32];
    char channel_method[32];
    char index[10];
    char *addr;
    char port[33];
    int pos, ret;

    if (is_channel == false) {
        /* parse service id */
        ret = sscanf(str,"%32[^:]:%n",service_id,&pos);
        if (ret != 1) {
            warn("error parsing service id");
            return -1;
        }
        qemu_opt_set(opts, "service_id", service_id);
    } else {
        /* parse connection type */
        ret = sscanf(str,"%32[^:]:%n",channel_method,&pos);
        if (ret != 1) {
            warn("error parsing channel method");
            return -1;
        }
        qemu_opt_set(opts, "channel_method", channel_method);
    }
    str += pos;
    pos = 0;

    /* parse path/addr and port */
    if (str[0] == '[') {
        /* ipv6 formatted */
        ret = sscanf(str,"[%a[^]:]]:%32[^:]%n",&addr,port,&pos);
        qemu_opt_set(opts, "ipv6", "on");
    } else {
        ret = sscanf(str,"%a[^:]:%32[^:]%n",&addr,port,&pos);
        qemu_opt_set(opts, "ipv4", "on");
    }

    if (ret != 2) {
        warnx("error parsing path/addr/port");
        return -1;
    } else if (port[0] == '-') {
        /* no port given, assume unix path */
        qemu_opt_set(opts, "path", addr);
    } else {
        qemu_opt_set(opts, "host", addr);
        qemu_opt_set(opts, "port", port);
        qemu_free(addr);
    }
    str += pos;
    pos = 0;

    if (str[0] == ':') {
        /* parse optional index parameter */
        ret = sscanf(str,":%10[^:]%n",index,&pos);
    } else {
        qemu_opt_set(opts, "index", "0");
        return 0;
    }

    if (ret != 1) {
        warnx("error parsing index");
        return -1;
    } else {
        qemu_opt_set(opts, "index", index);
    }
    str += pos;
    pos = 0;

    return 0;
}

static VPDriver *get_channel_drv(int index) {
    VPData *data;
    VPDriver *drv;
    int cindex;

    QTAILQ_FOREACH(data, &channels, next) {
        cindex = qemu_opt_get_number(data->opts, "index", 0);
        if (cindex == index) {
            drv = data->opaque;
            return drv;
        }
    }

    return NULL;
}

static int init_channels(void) {
    VPDriver *drv;
    VPData *channel_data;
    const char *channel_method, *path;
    int fd, ret;
    bool listen;

    if (QTAILQ_EMPTY(&channels)) {
        warnx("no channel specified");
        return -1;
    }

    channel_data = QTAILQ_FIRST(&channels);

    /* TODO: add this support, optional idx param for -i/-o/-c
     * args should suffice
     */
    if (QTAILQ_NEXT(channel_data, next) != NULL) {
        warnx("multiple channels not currently supported, defaulting to first");
    }

    INFO("initializing channel...");
    if (verbose_enabled) {
        qemu_opts_print(channel_data->opts, NULL);
    }

    channel_method = qemu_opt_get(channel_data->opts, "channel_method");

    if (strcmp(channel_method, "tcp-listen") == 0) {
        fd = inet_listen_opts(channel_data->opts, 0);
        listen = true;
    } else if (strcmp(channel_method, "tcp-connect") == 0) {
        fd = inet_connect_opts(channel_data->opts);
        listen = false;
    } else if (strcmp(channel_method, "unix-listen") == 0) {
        fd = unix_listen_opts(channel_data->opts);
        listen = true;
    } else if (strcmp(channel_method, "unix-connect") == 0) {
        fd = unix_connect_opts(channel_data->opts);
        listen = false;
    } else if (strcmp(channel_method, "virtserial-open") == 0) {
        path = qemu_opt_get(channel_data->opts, "path");
        fd = qemu_open(path, O_RDWR);
        ret = fcntl(fd, F_GETFL);
        ret = fcntl(fd, F_SETFL, ret | O_ASYNC);
        if (ret < 0) {
            warn("error setting flags for fd");
            return -1;
        }
        listen = false;
    } else {
        warnx("invalid channel type: %s", channel_method);
        return -1;
    }

    if (fd == -1) {
        warn("error opening connection");
        return -1;
    }

    drv = vp_new(fd, listen);
    channel_data->opaque = drv;

    return 0;
}

static int init_oforwards(void) {
    VPDriver *drv;
    VPData *oforward_data;
    int index, ret, fd;
    const char *service_id;

    QTAILQ_FOREACH(oforward_data, &oforwards, next) {
        INFO("initializing oforward...");
        if (verbose_enabled) {
            qemu_opts_print(oforward_data->opts, NULL);
        }

        index = qemu_opt_get_number(oforward_data->opts, "index", 0);
        drv = get_channel_drv(index);
        if (drv == NULL) {
            warnx("unable to find channel with index: %d", index);
            return -1;
        }

        if (qemu_opt_get(oforward_data->opts, "host") != NULL) {
            fd = inet_listen_opts(oforward_data->opts, 0);
        } else if (qemu_opt_get(oforward_data->opts, "path") != NULL) {
            fd = unix_listen_opts(oforward_data->opts);
        } else {
            warnx("unable to find listening socket host/addr info");
            return -1;
        }

        if (fd == -1) {
            warnx("failed to create FD");
            return -1;
        }

        service_id = qemu_opt_get(oforward_data->opts, "service_id");

        if (service_id == NULL) {
            warnx("no service_id specified");
            return -1;
        }

        ret = vp_set_oforward(drv, fd, service_id);
    }

    return 0;
}

static int init_iforwards(void) {
    VPDriver *drv;
    VPData *iforward_data;
    int index, ret;
    const char *service_id, *addr, *port;
    bool ipv6;

    QTAILQ_FOREACH(iforward_data, &iforwards, next) {
        INFO("initializing iforward...");
        if (verbose_enabled) {
            qemu_opts_print(iforward_data->opts, NULL);
        }

        index = qemu_opt_get_number(iforward_data->opts, "index", 0);
        drv = get_channel_drv(index);
        if (drv == NULL) {
            warnx("unable to find channel with index: %d", index);
            return -1;
        }

        service_id = qemu_opt_get(iforward_data->opts, "service_id");
        if (service_id == NULL) {
            warnx("no service_id specified");
            return -1;
        }

        addr = qemu_opt_get(iforward_data->opts, "path");
        port = NULL;

        if (addr == NULL) {
            /* map service to a network socket instead */
            addr = qemu_opt_get(iforward_data->opts, "host");
            port = qemu_opt_get(iforward_data->opts, "port");
        }

        ipv6 = qemu_opt_get_bool(iforward_data->opts, "ipv6", 0) ?
               true : false;

        ret = vp_set_iforward(drv, service_id, addr, port, ipv6);
        if (ret != 0) {
            warnx("error adding iforward");
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    const char *sopt = "hVvi:o:c:";
    struct option lopt[] = {
        { "help", 0, NULL, 'h' },
        { "version", 0, NULL, 'V' },
        { "verbose", 0, NULL, 'v' },
        { "iforward", 0, NULL, 'i' },
        { "oforward", 0, NULL, 'o' },
        { "channel", 0, NULL, 'c' },
        { NULL, 0, NULL, 0 }
    };
    int opt_ind = 0, ch, ret;
    QTAILQ_INIT(&iforwards);
    QTAILQ_INIT(&oforwards);
    QTAILQ_INIT(&channels);

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        QemuOpts *opts;
        VPData *data;
        switch (ch) {
        case 'i':
            opts = qemu_opts_create(&vp_opts, NULL, 0);
            ret = vp_parse(opts, optarg, 0);
            if (ret) {
                errx(EXIT_FAILURE, "error parsing arg: %s", optarg);
            }
            data = qemu_mallocz(sizeof(VPData));
            data->opts = opts;
            QTAILQ_INSERT_TAIL(&iforwards, data, next);
            break;
        case 'o':
            opts = qemu_opts_create(&vp_opts, NULL, 0);
            ret = vp_parse(opts, optarg, 0);
            if (ret) {
                errx(EXIT_FAILURE, "error parsing arg: %s", optarg);
            }
            data = qemu_mallocz(sizeof(VPData));
            data->opts = opts;
            QTAILQ_INSERT_TAIL(&oforwards, data, next);
            break;
        case 'c':
            opts = qemu_opts_create(&vp_opts, NULL, 0);
            ret = vp_parse(opts, optarg, 1);
            if (ret) {
                errx(EXIT_FAILURE, "error parsing arg: %s", optarg);
            }
            data = qemu_mallocz(sizeof(VPData));
            data->opts = opts;
            QTAILQ_INSERT_TAIL(&channels, data, next);
            break;
        case 'v':
            verbose_enabled = 1;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        case '?':
            errx(EXIT_FAILURE, "Try '%s --help' for more information.",
                 argv[0]);
        }
    }

    ret = init_channels();
    if (ret) {
        errx(EXIT_FAILURE, "error initializing communication channel");
    }

    ret = init_oforwards();
    if (ret) {
        errx(EXIT_FAILURE,
             "error initializing forwarders for outgoing connections");
    }

    ret = init_iforwards();
    if (ret) {
        errx(EXIT_FAILURE,
             "error initializing service mappings for incoming connections");
    }

    /* main i/o loop */
    for (;;) {
        DEBUG("entering main_loop_wait()");
        main_loop_wait(0);
        DEBUG("left main_loop_wait()");
    }

    return 0;
}
