/*
 * virtagent - common host/guest RPC functions
 *
 * Copyright IBM Corp. 2010
 *
 * Authors:
 *  Adam Litke        <aglitke@linux.vnet.ibm.com>
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "virtagent-common.h"

VAState *va_state;

/* helper to avoid tedious key/type checking on QDict entries */
bool va_qdict_haskey_with_type(const QDict *qdict, const char *key,
                               qtype_code type)
{
    QObject *qobj;
    if (!qdict) {
        return false;
    }
    if (!qdict_haskey(qdict, key)) {
        return false;
    }
    qobj = qdict_get(qdict, key);
    if (qobject_type(qobj) != type) {
        return false;
    }

    return true;
}

static void va_qdict_insert(const char *key, QObject *entry, void *opaque)
{
    QDict *dict = opaque;

    if (key && entry) {
        qdict_put_obj(dict, key, entry);
    }
}

QDict *va_qdict_copy(const QDict *old)
{
    QDict *new;

    if (!old) {
        return NULL;
    }

    new = qdict_new();
    qdict_iter(old, va_qdict_insert, new);

    return new;
}

void va_process_jobs(void)
{
    va_kick(va_state->manager);
}

static int va_connect(void)
{
    QemuOpts *opts;
    int fd, ret = 0;

    TRACE("called");
    if (va_state->channel_method == NULL) {
        LOG("no channel method specified");
        return -EINVAL;
    }
    if (va_state->channel_path == NULL) {
        LOG("no channel path specified");
        return -EINVAL;
    }

    if (strcmp(va_state->channel_method, "unix-connect") == 0) {
        TRACE("connecting to %s", va_state->channel_path);
        opts = qemu_opts_create(qemu_find_opts("chardev"), NULL, 0);
        qemu_opt_set(opts, "path", va_state->channel_path);
        fd = unix_connect_opts(opts);
        if (fd == -1) {
            qemu_opts_del(opts);
            LOG("error opening channel: %s", strerror(errno));
            return -errno;
        }
        qemu_opts_del(opts);
        socket_set_nonblock(fd);
    } else if (strcmp(va_state->channel_method, "virtio-serial") == 0) {
        if (va_state->is_host) {
            LOG("specified channel method not available for host");
            return -EINVAL;
        }
        if (va_state->channel_path == NULL) {
            va_state->channel_path = VA_GUEST_PATH_VIRTIO_DEFAULT;
        }
        TRACE("opening %s", va_state->channel_path);
        fd = qemu_open(va_state->channel_path, O_RDWR);
        if (fd == -1) {
            LOG("error opening channel: %s", strerror(errno));
            return -errno;
        }
        ret = fcntl(fd, F_GETFL);
        if (ret < 0) {
            LOG("error getting channel flags: %s", strerror(errno));
            return -errno;
        }
        ret = fcntl(fd, F_SETFL, ret | O_ASYNC | O_NONBLOCK);
        if (ret < 0) {
            LOG("error setting channel flags: %s", strerror(errno));
            return -errno;
        }
    } else if (strcmp(va_state->channel_method, "isa-serial") == 0) {
        struct termios tio;
        if (va_state->is_host) {
            LOG("specified channel method not available for host");
            return -EINVAL;
        }
        if (va_state->channel_path == NULL) {
            LOG("you must specify the path of the serial device to use");
            return -EINVAL;
        }
        TRACE("opening %s", va_state->channel_path);
        fd = qemu_open(va_state->channel_path, O_RDWR | O_NOCTTY);
        if (fd == -1) {
            LOG("error opening channel: %s", strerror(errno));
            return -errno;
        }
        tcgetattr(fd, &tio);
        /* set up serial port for non-canonical, dumb byte streaming */
        tio.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP |
                         INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY | IMAXBEL);
        tio.c_oflag = 0;
        tio.c_lflag = 0;
        tio.c_cflag |= VA_BAUDRATE;
        /* 1 available byte min, else reads will block (we'll set non-blocking
         * elsewhere, else we'd have to deal with read()=0 instead)
         */
        tio.c_cc[VMIN] = 1;
        tio.c_cc[VTIME] = 0;
        /* flush everything waiting for read/xmit, it's garbage at this point */
        tcflush(fd, TCIFLUSH);
        tcsetattr(fd, TCSANOW, &tio);
    } else {
        LOG("invalid channel method");
        return -EINVAL;
    }

    va_state->fd = fd;
    return 0;
}

int va_init(VAContext ctx)
{
    VAState *s;
    VAManager *m;
    int ret;

    TRACE("called");
    if (va_state) {
        LOG("virtagent already initialized");
        return -EPERM;
    }

    s = qemu_mallocz(sizeof(VAState));
    m = va_manager_new();

    ret = va_server_init(m, &s->server_data, ctx.is_host);
    if (ret) {
        LOG("error initializing virtagent server");
        goto out_bad;
    }
    ret = va_client_init(m, &s->client_data);
    if (ret) {
        LOG("error initializing virtagent client");
        goto out_bad;
    }

    s->client_job_count = 0;
    s->client_jobs_in_flight = 0;
    s->server_job_count = 0;
    s->channel_method = ctx.channel_method;
    s->channel_path = ctx.channel_path;
    s->is_host = ctx.is_host;
    s->manager = m;
    va_state = s;

    /* connect to our end of the channel */
    ret = va_connect();
    if (ret) {
        LOG("error connecting to channel");
        goto out_bad;
    }

    /* start listening for requests/responses */
    qemu_set_fd_handler(va_state->fd, va_http_read_handler, NULL, NULL);

    if (!va_state->is_host) {
        /* tell the host the agent is running */
        va_send_hello();
    }

    return 0;
out_bad:
    qemu_free(s);
    return ret;
}
