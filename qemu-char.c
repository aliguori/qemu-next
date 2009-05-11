/*
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
#include "qemu-common.h"
#include "net.h"
#include "monitor.h"
#include "console.h"
#include "sysemu.h"
#include "qemu-timer.h"
#include "qemu-char.h"
#include "block.h"
#include "hw/usb.h"
#include "hw/baum.h"
#include "hw/msmouse.h"

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <zlib.h>

#ifndef _WIN32
#include <sys/times.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#ifdef __NetBSD__
#include <net/if_tap.h>
#endif
#ifdef __linux__
#include <linux/if_tun.h>
#endif
#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/select.h>
#ifdef HOST_BSD
#include <sys/stat.h>
#ifdef __FreeBSD__
#include <libutil.h>
#include <dev/ppbus/ppi.h>
#include <dev/ppbus/ppbconf.h>
#elif defined(__DragonFly__)
#include <libutil.h>
#include <dev/misc/ppi/ppi.h>
#include <bus/ppbus/ppbconf.h>
#else
#include <util.h>
#endif
#elif defined (__GLIBC__) && defined (__FreeBSD_kernel__)
#include <freebsd/stdlib.h>
#else
#ifdef __linux__
#include <pty.h>

#include <linux/ppdev.h>
#include <linux/parport.h>
#endif
#ifdef __sun__
#include <sys/stat.h>
#include <sys/ethernet.h>
#include <sys/sockio.h>
#include <netinet/arp.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h> // must come after ip.h
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <syslog.h>
#include <stropts.h>
#endif
#endif
#endif

#include "qemu_socket.h"

/***********************************************************/
/* character device */

static TAILQ_HEAD(CharDriverStateHead, CharDriverState) chardevs =
    TAILQ_HEAD_INITIALIZER(chardevs);
static int initial_reset_issued;

static void qemu_chr_event(CharDriverState *s, int event)
{
    if (!s->chr_event)
        return;
    s->chr_event(s->handler_opaque, event);
}

static void qemu_chr_reset_bh(void *opaque)
{
    CharDriverState *s = opaque;
    qemu_chr_event(s, CHR_EVENT_RESET);
    qemu_bh_delete(s->bh);
    s->bh = NULL;
}

void qemu_chr_reset(CharDriverState *s)
{
    if (s->bh == NULL && initial_reset_issued) {
	s->bh = qemu_bh_new(qemu_chr_reset_bh, s);
	qemu_bh_schedule(s->bh);
    }
}

void qemu_chr_initial_reset(void)
{
    CharDriverState *chr;

    initial_reset_issued = 1;

    TAILQ_FOREACH(chr, &chardevs, next) {
        qemu_chr_reset(chr);
    }
}

int qemu_chr_write(CharDriverState *s, const uint8_t *buf, int len)
{
    return s->chr_write(s, buf, len);
}

int qemu_chr_ioctl(CharDriverState *s, int cmd, void *arg)
{
    if (!s->chr_ioctl)
        return -ENOTSUP;
    return s->chr_ioctl(s, cmd, arg);
}

int qemu_chr_can_read(CharDriverState *s)
{
    if (!s->chr_can_read)
        return 0;
    return s->chr_can_read(s->handler_opaque);
}

void qemu_chr_read(CharDriverState *s, uint8_t *buf, int len)
{
    s->chr_read(s->handler_opaque, buf, len);
}

void qemu_chr_accept_input(CharDriverState *s)
{
    if (s->chr_accept_input)
        s->chr_accept_input(s);
}

void qemu_chr_printf(CharDriverState *s, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    qemu_chr_write(s, (uint8_t *)buf, strlen(buf));
    va_end(ap);
}

void qemu_chr_send_event(CharDriverState *s, int event)
{
    if (s->chr_send_event)
        s->chr_send_event(s, event);
}

void qemu_chr_add_handlers(CharDriverState *s,
                           IOCanRWHandler *fd_can_read,
                           IOReadHandler *fd_read,
                           IOEventHandler *fd_event,
                           void *opaque)
{
    s->chr_can_read = fd_can_read;
    s->chr_read = fd_read;
    s->chr_event = fd_event;
    s->handler_opaque = opaque;
    if (s->chr_update_read_handler)
        s->chr_update_read_handler(s);
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

static int unix_write(int fd, const uint8_t *buf, int len1)
{
    int ret, len;

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

int send_all(int fd, const void *buf, int len1)
{
    return unix_write(fd, buf, len1);
}
#endif /* !_WIN32 */

#ifndef _WIN32

#if defined(__linux__)
#else /* _WIN32 */

static int win_chr_pipe_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    WinCharState *s = chr->opaque;
    DWORD size;

    PeekNamedPipe(s->hcom, NULL, 0, NULL, &size, NULL);
    if (size > 0) {
        s->len = size;
        win_chr_read_poll(chr);
        win_chr_read(chr);
        return 1;
    }
    return 0;
}

static int win_chr_pipe_init(CharDriverState *chr, const char *filename)
{
    WinCharState *s = chr->opaque;
    OVERLAPPED ov;
    int ret;
    DWORD size;
    char openname[256];

    s->fpipe = TRUE;

    s->hsend = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hsend) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }
    s->hrecv = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hrecv) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }

    snprintf(openname, sizeof(openname), "\\\\.\\pipe\\%s", filename);
    s->hcom = CreateNamedPipe(openname, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
                              PIPE_WAIT,
                              MAXCONNECT, NSENDBUF, NRECVBUF, NTIMEOUT, NULL);
    if (s->hcom == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed CreateNamedPipe (%lu)\n", GetLastError());
        s->hcom = NULL;
        goto fail;
    }

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ret = ConnectNamedPipe(s->hcom, &ov);
    if (ret) {
        fprintf(stderr, "Failed ConnectNamedPipe\n");
        goto fail;
    }

    ret = GetOverlappedResult(s->hcom, &ov, &size, TRUE);
    if (!ret) {
        fprintf(stderr, "Failed GetOverlappedResult\n");
        if (ov.hEvent) {
            CloseHandle(ov.hEvent);
            ov.hEvent = NULL;
        }
        goto fail;
    }

    if (ov.hEvent) {
        CloseHandle(ov.hEvent);
        ov.hEvent = NULL;
    }
    qemu_add_polling_cb(win_chr_pipe_poll, chr);
    return 0;

 fail:
    win_chr_close(chr);
    return -1;
}


static CharDriverState *qemu_chr_open_win_pipe(const char *filename)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_mallocz(sizeof(CharDriverState));
    s = qemu_mallocz(sizeof(WinCharState));
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    chr->chr_close = win_chr_close;

    if (win_chr_pipe_init(chr, filename) < 0) {
        free(s);
        free(chr);
        return NULL;
    }
    qemu_chr_reset(chr);
    return chr;
}

static CharDriverState *qemu_chr_open_win_file(HANDLE fd_out)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_mallocz(sizeof(CharDriverState));
    s = qemu_mallocz(sizeof(WinCharState));
    s->hcom = fd_out;
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    qemu_chr_reset(chr);
    return chr;
}

static CharDriverState *qemu_chr_open_win_con(const char *filename)
{
    return qemu_chr_open_win_file(GetStdHandle(STD_OUTPUT_HANDLE));
}

static CharDriverState *qemu_chr_open_win_file_out(const char *file_out)
{
    HANDLE fd_out;

    fd_out = CreateFile(file_out, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fd_out == INVALID_HANDLE_VALUE)
        return NULL;

    return qemu_chr_open_win_file(fd_out);
}
#endif /* !_WIN32 */

typedef struct CharDriverEntry
{
    CharDriver *drv;
    TAILQ_ENTRY(CharDriverEntry) node;
} CharDriverEntry;

static TAILQ_HEAD(, CharDriverEntry) char_drivers = TAILQ_HEAD_INITIALIZER(char_drivers);

int qemu_chr_register_driver(CharDriver *drv)
{
    CharDriverEntry *e;

    e = qemu_mallocz(sizeof(*e));
    e->drv = drv;
    TAILQ_INSERT_HEAD(&char_drivers, e, node);

    return 0;
}

void qemu_chr_unregister_driver(CharDriver *drv)
{
    CharDriverEntry *e;

    TAILQ_FOREACH(e, &char_drivers, node) {
        if (e->drv == drv)
            break;
    }

    if (e) {
        TAILQ_REMOVE(&char_drivers, e, node);
        qemu_free(e);
    }
}

CharDriverState *qemu_chr_open(const char *label, const char *filename, void (*init)(struct CharDriverState *s))
{
    const char *p = NULL;
    CharDriverState *chr = NULL;
    CharDriverEntry *e;

    TAILQ_FOREACH(e, &char_drivers, node) {
        if ((e->drv->flags & CHAR_DRIVER_NO_ARGS)) {
            if (strcmp(filename, e->drv->name) == 0) {
                break;
            }
        } else {
            if (strstart(filename, e->drv->name, &p)) {
                break;
            }
        }
    }

    if (e) {
        if (e->drv->flags & CHAR_DRIVER_FULL_ARGS)
            p = filename;

        chr = e->drv->init(label, p);
        if (!chr->filename)
            chr->filename = qemu_strdup(filename);
        chr->init = init;
        chr->label = qemu_strdup(label);
        TAILQ_INSERT_TAIL(&chardevs, chr, next);
    }

    return chr;
}

#ifdef _WIN32

static CharDriverState *chr_drv_pipe_init(const char *label, const char *filename)
{
    return qemu_chr_open_win_pipe(filename);
}

static CharDriver chr_drv_pipe = {
    .name = "pipe:",
    .init = chr_drv_pipe_init,
};

static CharDriverState *chr_drv_con_init(const char *label, const char *filename)
{
    return qemu_chr_open_win_con(filename);
}

static CharDriver chr_drv_con = {
    .name = "con:",
    .init = chr_drv_con_init,
};

static CharDriverState *chr_drv_file_init(const char *label, const char *filename)
{
    return qemu_chr_open_win_file_out(filename);
}

static CharDriver chr_drv_file = {
    .name = "file:",
    .init = chr_drv_file_init,
};
#endif

int qemu_chr_drv_init(void)
{
    return module_call_init(MOD_PRI_CHAR_DRIVER);
}

void qemu_chr_close(CharDriverState *chr)
{
    TAILQ_REMOVE(&chardevs, chr, next);
    if (chr->chr_close)
        chr->chr_close(chr);
    qemu_free(chr->filename);
    qemu_free(chr->label);
    qemu_free(chr);
}

void qemu_chr_info(Monitor *mon)
{
    CharDriverState *chr;

    TAILQ_FOREACH(chr, &chardevs, next) {
        monitor_printf(mon, "%s: filename=%s\n", chr->label, chr->filename);
    }
}
