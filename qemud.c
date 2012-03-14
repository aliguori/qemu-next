#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "osdep.h"
#include "qemu_socket.h"

typedef struct Client
{
    GIOChannel *chan;
    GString *tx;
    int fd;
    int watch;
} Client;

#define QEMUD_INFO   0
#define QEMUD_WARN   1
#define QEMUD_ERROR  2

static void qemud_log(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static gboolean foreground = FALSE;
static gchar *socket_filename = NULL;

static GOptionEntry entries[] = {
    { "foreground", 'f', 0, G_OPTION_ARG_NONE, &foreground,
      "Run in the foreground instead of daemonizing", NULL },
    { "socket", 's', 0, G_OPTION_ARG_FILENAME, &socket_filename,
      "Unix socket to listen on for incoming connections", "PATH" },
    { NULL }
};

static void qemud_log(int level, const char *fmt, ...)
{
    const char *status[] = {
        [QEMUD_INFO] = "INFO",
        [QEMUD_WARN] = "WARNING",
        [QEMUD_ERROR] = "ERROR",
    };
    qemu_timeval tv;
    time_t now;
    struct tm tm;
    va_list ap;
    int old_errno = errno;

    qemu_gettimeofday(&tv);
    now = tv.tv_sec;
    localtime_r(&now, &tm);

    fprintf(stderr, "[%s %02d:%02d:%02d.%06ld] ", status[level],
            tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec);

    va_start(ap, fmt);
    errno = old_errno;
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
}

static void qemud_client_free(Client *c)
{
    qemud_log(QEMUD_INFO, "Closing client %d\n", c->fd);
    close(c->fd);
    g_io_channel_unref(c->chan);
    g_source_remove(c->watch);
    g_free(c);
}

static gboolean qemud_client_recv(GIOChannel *chan, GIOCondition cond, gpointer user_data)
{
    Client *c = user_data;
    char buffer[1024];
    ssize_t len;

    len = read(c->fd, buffer, sizeof(buffer));
    if (len == -1) {
        if (errno == EINTR || errno == EAGAIN) {
            return TRUE;
        }
        qemud_log(QEMUD_WARN, "received error on fd %d: %m", c->fd);
        qemud_client_free(c);
        return FALSE;
    } else if (len == 0) {
        qemud_log(QEMUD_INFO, "received EOF on %d", c->fd);
        qemud_client_free(c);
        return FALSE;
    }

    qemud_log(QEMUD_INFO, "received %ld byte(s) on fd %d", len, c->fd);

    return TRUE;
}

static Client *qemud_client_new(int fd)
{
    Client *c = g_malloc0(sizeof(*c));

    c->fd = fd;
    socket_set_nonblock(c->fd);
    c->chan = g_io_channel_unix_new(c->fd);
    c->watch = g_io_add_watch(c->chan,
                              G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                              qemud_client_recv, c);

    return c;
}

static gboolean qemud_accept(GIOChannel *chan, GIOCondition cond, gpointer user_data)
{
    int s = g_io_channel_unix_get_fd(chan);
    int fd;
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);
    Client *c;

    fd = qemu_accept(s, (struct sockaddr *)&addr, &addrlen);
    if (fd == -1) {
        qemud_log(QEMUD_WARN, "failed to accept client with status: %m");
        return TRUE;
    }

    c = qemud_client_new(fd);

    qemud_log(QEMUD_INFO, "accepted new client with fd of %d", fd);

    return TRUE;
}

int main(int argc, char **argv)
{
    GError *error = NULL;
    GOptionContext *context;
    GMainLoop *loop;
    GIOChannel *chan;
    int s;

    context = g_option_context_new("- daemon for managing QEMU instances");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("option parsing failed: %s\n", error->message);
        exit(1);
    }

    if (socket_filename == NULL) {
        socket_filename = g_strdup("/var/run/qemud/qemud.sock");
    }

    s = unix_listen(socket_filename, NULL, 0);
    g_assert(s != -1);

    socket_set_nonblock(s);

    chan = g_io_channel_unix_new(s);

    loop = g_main_loop_new(g_main_context_default(), FALSE);

    if (!foreground) {
        int ret;

        ret = qemu_daemon(0, 0);
        g_assert(ret == 0);
    }

    /*
     * 2) create a QMP session on said socket
     * 3) provide a create command to launch a new guest
     *    a) accept a dictionary of dictionaries roughly corresponding to 
     *       an INI file.  Have a single section for legacy options and another
     *       section for non-legacy options.
     * 4) provide a mechanism to enumerate running guests
     * 5) provide a mechanism to send QMP commands directly to running guests
     *
     * 6) provide a user level tool that exposes this API
     */

    g_io_add_watch(chan,
                   G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                   qemud_accept, loop);

    g_main_loop_run(loop);

    g_free(socket_filename);

    return 0;
}
