#ifndef TCP_BASE_H
#define TCP_BASE_H

#include "chrdrv.h"

typedef struct TcpCharDriver
{
    CharDriver parent;

    int max_size;
    int do_telnetopt;
    int is_unix;
    int connected;
    int fd;
    int msgfd;
} TcpCharDriver;

#define TYPE_TCP_CHAR_DRIVER "tcp-char-driver"
#define TCP_CHAR_DRIVER(obj) TYPE_CHECK(TcpCharDriver, obj, TYPE_TCP_CHAR_DRIVER)

void tcp_char_driver_initialize(TcpCharDriver *obj, const char *id);
void tcp_char_driver_finalize(TcpCharDriver *obj);

#endif

#include "qemu_socket.h"

void tcp_char_driver_initialize(TcpCharDriver *obj, const char *id)
{
    type_initialize(obj, TYPE_TCP_CHAR_DRIVER, id);
}

void tcp_char_driver_finalize(TcpCharDriver *obj)
{
    type_finalize(obj);
}

static int tcp_char_driver_write(CharDriver *chr, const uint8_t *buf, int len)
{
    TcpCharDriver *s = TCP_CHAR_DRIVER(chr);

    if (s->connected) {
        return send_all(s->fd, buf, len);
    }

    return len;
}
    
static int tcp_char_driver_get_msgfd(CharDriver *chr)
{
    TcpCharDriver *s = TCP_CHAR_DRIVER(chr);
    int fd = s->msgfd;
    s->msgfd = -1;
    return fd;
}

#define IAC 255
#define IAC_BREAK 243
static void tcp_chr_process_IAC_bytes(TcpCharDriver *s,
                                      uint8_t *buf, int *size)
{
    /* Handle any telnet client's basic IAC options to satisfy char by
     * char mode with no echo.  All IAC options will be removed from
     * the buf and the do_telnetopt variable will be used to track the
     * state of the width of the IAC information.
     *
     * IAC commands come in sets of 3 bytes with the exception of the
     * "IAC BREAK" command and the double IAC.
     */

    int i;
    int j = 0;

    for (i = 0; i < *size; i++) {
        if (s->do_telnetopt > 1) {
            if ((unsigned char)buf[i] == IAC && s->do_telnetopt == 2) {
                /* Double IAC means send an IAC */
                if (j != i)
                    buf[j] = buf[i];
                j++;
                s->do_telnetopt = 1;
            } else {
                if ((unsigned char)buf[i] == IAC_BREAK && s->do_telnetopt == 2) {
                    /* Handle IAC break commands by sending a serial break */
                    char_driver_event(CHAR_DRIVER(s), CHR_EVENT_BREAK);
                    s->do_telnetopt++;
                }
                s->do_telnetopt++;
            }
            if (s->do_telnetopt >= 4) {
                s->do_telnetopt = 1;
            }
        } else {
            if ((unsigned char)buf[i] == IAC) {
                s->do_telnetopt = 2;
            } else {
                if (j != i)
                    buf[j] = buf[i];
                j++;
            }
        }
    }
    *size = j;
}

#ifndef _WIN32
static void unix_process_msgfd(CharDriver *chr, struct msghdr *msg)
{
    TcpCharDriver *s = TCP_CHAR_DRIVER(chr);
    struct cmsghdr *cmsg;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        int fd;

        if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ||
            cmsg->cmsg_level != SOL_SOCKET ||
            cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }

        fd = *((int *)CMSG_DATA(cmsg));
        if (fd < 0) {
            continue;
        }

        if (s->msgfd != -1) {
            close(s->msgfd);
        }
        s->msgfd = fd;
    }
}

static ssize_t tcp_char_driver_recv(CharDriver *chr, char *buf, size_t len)
{
    TcpCharDriver *s = TCP_CHAR_DRIVER(chr);
    struct msghdr msg = { NULL, };
    struct iovec iov[1];
    union {
        struct cmsghdr cmsg;
        char control[CMSG_SPACE(sizeof(int))];
    } msg_control;
    ssize_t ret;

    iov[0].iov_base = buf;
    iov[0].iov_len = len;

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = &msg_control;
    msg.msg_controllen = sizeof(msg_control);

    ret = recvmsg(s->fd, &msg, 0);
    if (ret > 0 && s->is_unix) {
        unix_process_msgfd(chr, &msg);
    }

    return ret;
}
#else
static ssize_t tcp_char_driver_recv(CharDriver *chr, char *buf, size_t len)
{
    TcpCharDriver *s = TCP_CHAR_DRIVER(chr);
    return recv(s->fd, buf, len, 0);
}
#endif

#define READ_BUF_LEN 1024

static void tcp_char_driver_read(void *opaque)
{
    TcpCharDriver *s = opaque;
    uint8_t buf[READ_BUF_LEN];
    int len, size;

    if (!s->connected || s->max_size <= 0) {
        return;
    }
    len = sizeof(buf);
    if (len > s->max_size) {
        len = s->max_size;
    }
    size = tcp_char_driver_recv(CHAR_DRIVER(s), (void *)buf, len);
    if (size == 0) {
        /* connection closed */
        s->connected = 0;
        if (s->listen_fd >= 0) {
            qemu_set_fd_handler(s->listen_fd, tcp_chr_accept, NULL, chr);
        }
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
        s->fd = -1;
        char_driver_event(chr, CHR_EVENT_CLOSED);
    } else if (size > 0) {
        if (s->do_telnetopt) {
            tcp_char_driver_process_IAC_bytes(chr, s, buf, &size);
        }
        if (size > 0) {
            char_driver_read(chr, buf, size);
        }
    }
}

static void tcp_char_driver_class_init(TypeClass *class)
{
    CharDriverClass *cdc = CHAR_DRIVER_CLASS(class);

    cdc->write = tcp_char_driver_write;
    cdc->close = tcp_char_driver_close;
    cdc->get_msgfd = tcp_char_driver_msgfd;
}

static TypeInfo tcp_char_driver_info = {
    .name = TYPE_TCP_CHAR_DRIVER,
    .parent = TYPE_CHAR_DRIVER,
    .instance_size = sizeof(TcpCharDriver),
    .class_size = sizeof(TcpCharDriverClass),
    .class_init = tcp_char_driver_class_init,
};

static void register_backends(void)
{
    type_register_static(&tcp_char_driver_info);
}

device_init(register_backends);
