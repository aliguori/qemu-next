/*
 * virtagent - QEMU guest agent
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
#include "qemu-ioh.h"
#include "virtagent-common.h"

static bool verbose_enabled;
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

QemuOptsList va_opts = {
    .name = "vaargs",
    .head = QTAILQ_HEAD_INITIALIZER(va_opts.head),
    .desc = {
        {
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
        { /* end of list */ }
    },
};

/* parse channel options */
static int va_parse(QemuOpts *opts, const char *str)
{
    char channel_method[32];
    char *addr;
    char port[33];
    int pos, ret;

    /* parse connection type */
    ret = sscanf(str,"%32[^:]:%n",channel_method,&pos);
    if (ret != 1) {
        LOG("error parsing channel method");
        return -EINVAL;
    }
    qemu_opt_set(opts, "channel_method", channel_method);
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
        LOG("error parsing path/addr/port");
        return -EINVAL;
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

/* mirror qemu I/O loop for standalone daemon */
static void main_loop_wait(int nonblocking)
{
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
    qemu_get_fdset(&nfds, &rfds, &wfds, &xfds);

    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    ret = select(nfds + 1, &rfds, &wfds, &xfds, &tv);

    if (ret > 0) {
        qemu_process_fd_handlers(&rfds, &wfds, &xfds);
    }
}

#define VP_ARG_LEN 256

typedef struct VAData {
    QemuOpts *opts;
    void *opaque;
    QTAILQ_ENTRY(VAData) next;
} VAData;

static QTAILQ_HEAD(, VAData) channels;

static void usage(const char *cmd)
{
    printf(
"Usage: %s -c <channel_opts>\n"
"QEMU virtagent guest agent\n"
"\n"
"  -c, --channel     channel options of the form:\n"
"                    <method>:<addr>:<port>[:channel_id]\n"
"  -v, --verbose     display extra debugging information\n"
"  -h, --help        display this help and exit\n"
"\n"
"  channels are used to establish a data connection between 2 end-points in\n"
"  the host or the guest (connection method specified by <method>).\n"
"  The positional parameters for channels are:\n"
"\n"
"  <method>:     one of unix-connect, unix-listen, tcp-connect, tcp-listen,\n"
"                virtserial-open\n"
"  <addr>:       path of unix socket or virtserial port, or IP of host, to\n"
"                connect/bind to\n"
"  <port>:       port to bind/connect to, or '-' if addr is a path\n"
"\n"
"Report bugs to <mdroth@linux.vnet.ibm.com>\n"
    , cmd);
}

static int init_channels(void) {
    VAData *channel_data;
    const char *channel_method, *path;
    int fd, ret;
    bool listen;

    if (QTAILQ_EMPTY(&channels)) {
        warnx("no channel specified");
        return -EINVAL;
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
    } else if (strcmp(channel_method, "virtio-serial") == 0) {
        path = qemu_opt_get(channel_data->opts, "path");
        fd = qemu_open(path, O_RDWR);
        if (fd == -1) {
            warn("error opening channel");
            return errno;
        }
        ret = fcntl(fd, F_GETFL);
        ret = fcntl(fd, F_SETFL, ret | O_ASYNC);
        if (ret < 0) {
            warn("error setting flags for fd");
            return errno;
        }
        listen = false;
    } else {
        warnx("invalid channel type: %s", channel_method);
        return -EINVAL;
    }

    if (fd == -1) {
        warn("error opening connection");
        return errno;
    }

    /* initialize virtagent */
    ret = va_init(VA_CTX_GUEST, fd);
    if (ret) {
        errx(EXIT_FAILURE, "unable to initialize virtagent");
    }


    return 0;
}

int main(int argc, char **argv)
{
    const char *sopt = "hVvc:";
    struct option lopt[] = {
        { "help", 0, NULL, 'h' },
        { "version", 0, NULL, 'V' },
        { "verbose", 0, NULL, 'v' },
        { "channel", 0, NULL, 'c' },
        { NULL, 0, NULL, 0 }
    };
    int opt_ind = 0, ch, ret;
    QTAILQ_INIT(&channels);

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        QemuOpts *opts;
        VAData *data;
        switch (ch) {
        case 'c':
            opts = qemu_opts_create(&va_opts, NULL, 0);
            ret = va_parse(opts, optarg);
            if (ret) {
                errx(EXIT_FAILURE, "error parsing arg: %s", optarg);
            }
            data = qemu_mallocz(sizeof(VAData));
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

    /* initialize communication channel and pass it to virtagent */
    /* XXX: we only support one channel now so this should be simplified */
    ret = init_channels();
    if (ret) {
        errx(EXIT_FAILURE, "error initializing communication channel");
    }

    /* tell the host the agent is running */
    va_send_hello();

    /* main i/o loop */
    for (;;) {
        DEBUG("entering main_loop_wait()");
        main_loop_wait(0);
        DEBUG("left main_loop_wait()");
    }

    return 0;
}
