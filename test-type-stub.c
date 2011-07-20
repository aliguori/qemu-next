#include "qemu-common.h"
#include "qerror.h"
#include "qemu/type.h"

#include "test-type-stub.h"
#include "qemu_socket.h"
#include "qemu-char.h"

#include <glib.h>

typedef void (module_initfn)(void);

static int num_modules_initfns;
static module_initfn *module_initfns[128];

void register_module_init(void (*fn)(void), module_init_type type)
{
    module_initfns[num_modules_initfns++] = fn;
}

QString *qerror_format(const char *fmt, QDict *error)
{
    return qstring_from_str("");
}

void test_type_stub_init(void)
{
    int i;

    type_system_init();

    for (i = 0; i < num_modules_initfns; i++) {
        module_initfns[i]();
    }
}

#ifdef _WIN32
int send_all(int fd, const void *buf, int len1)
{
    int ret, len;

    len = len1;
    while (len > 0) {
        ret = send(fd, buf, len, 0);
        if (ret < 0) {
            errno = WSAGetLastError();
            if (errno != WSAEWOULDBLOCK) {
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

#else

int send_all(int fd, const void *_buf, int len1)
{
    int ret, len;
    const uint8_t *buf = _buf;

    len = len1;
    while (len > 0) {
        ret = write(fd, buf, len);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN)
                return -1;
        } else if (ret == 0) {
            break;
        } else {
            buf += ret;
            len -= ret;
        }
    }
    return len1 - len;
}
#endif /* !_WIN32 */

#define MAX_FD 1024

typedef struct ChannelInfo
{
    GIOChannel *channel;
    guint tag;
    IOHandler *io_read;
    IOHandler *io_write;
    void *opaque;
} ChannelInfo;

static ChannelInfo io_channels[MAX_FD];

static gboolean io_dispatcher(GIOChannel *chan, GIOCondition cond, gpointer opaque)
{
    ChannelInfo *info = opaque;

    if ((cond & G_IO_IN)) {
        info->io_read(info->opaque);
    }

    if ((cond & G_IO_OUT)) {
        info->io_write(info->opaque);
    }

    return TRUE;
}

int qemu_set_fd_handler(int fd,
                        IOHandler *fd_read,
                        IOHandler *fd_write,
                        void *opaque)
{
    GIOCondition cond = 0;
    ChannelInfo *info = &io_channels[fd];

    if (info->channel) {
        g_source_remove(info->tag);
        g_io_channel_unref(info->channel);
    }

    info->channel = g_io_channel_unix_new(fd);
    info->io_read = fd_read;
    info->io_write = fd_write;
    info->opaque = opaque;

    if (fd_read) {
        cond |= G_IO_IN;
    }

    if (fd_write) {
        cond |= G_IO_OUT;
    }

    info->tag = g_io_add_watch(info->channel, cond,
                               io_dispatcher, info);

    return 0;
}
