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
 */

#include <getopt.h>
#include <err.h>
#include "qemu-option.h"
#include "virtproxy.h"

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
    int timeout = 1000;

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

static int vp_parse(QemuOpts *opts, const char *str, bool is_channel)
{
    /* TODO: use VP_SERVICE_ID_LEN, bring it into virtproxy.h */
    char service_id[32];
    char channel_method[32];
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
        case '?':
            errx(EXIT_FAILURE, "Try '%s --help' for more information.",
                 argv[0]);
        }
    }

    return 0;
}
