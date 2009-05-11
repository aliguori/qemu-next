/***********************************************************/
/* TCP Net console */

typedef struct {
    int fd, listen_fd;
    int connected;
    int max_size;
    int do_telnetopt;
    int do_nodelay;
    int is_unix;
} TCPCharDriver;

static void tcp_chr_accept(void *opaque);

static int tcp_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    TCPCharDriver *s = chr->opaque;
    if (s->connected) {
        return send_all(s->fd, buf, len);
    } else {
        /* XXX: indicate an error ? */
        return len;
    }
}

static int tcp_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    if (!s->connected)
        return 0;
    s->max_size = qemu_chr_can_read(chr);
    return s->max_size;
}

#define IAC 255
#define IAC_BREAK 243
static void tcp_chr_process_IAC_bytes(CharDriverState *chr,
                                      TCPCharDriver *s,
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
                    qemu_chr_event(chr, CHR_EVENT_BREAK);
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

static void tcp_chr_read(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    uint8_t buf[1024];
    int len, size;

    if (!s->connected || s->max_size <= 0)
        return;
    len = sizeof(buf);
    if (len > s->max_size)
        len = s->max_size;
    size = recv(s->fd, buf, len, 0);
    if (size == 0) {
        /* connection closed */
        s->connected = 0;
        if (s->listen_fd >= 0) {
            qemu_set_fd_handler(s->listen_fd, tcp_chr_accept, NULL, chr);
        }
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
        s->fd = -1;
    } else if (size > 0) {
        if (s->do_telnetopt)
            tcp_chr_process_IAC_bytes(chr, s, buf, &size);
        if (size > 0)
            qemu_chr_read(chr, buf, size);
    }
}

static void tcp_chr_connect(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;

    s->connected = 1;
    qemu_set_fd_handler2(s->fd, tcp_chr_read_poll,
                         tcp_chr_read, NULL, chr);
    qemu_chr_reset(chr);
}

#define IACSET(x,a,b,c) x[0] = a; x[1] = b; x[2] = c;
static void tcp_chr_telnet_init(int fd)
{
    char buf[3];
    /* Send the telnet negotion to put telnet in binary, no echo, single char mode */
    IACSET(buf, 0xff, 0xfb, 0x01);  /* IAC WILL ECHO */
    send(fd, (char *)buf, 3, 0);
    IACSET(buf, 0xff, 0xfb, 0x03);  /* IAC WILL Suppress go ahead */
    send(fd, (char *)buf, 3, 0);
    IACSET(buf, 0xff, 0xfb, 0x00);  /* IAC WILL Binary */
    send(fd, (char *)buf, 3, 0);
    IACSET(buf, 0xff, 0xfd, 0x00);  /* IAC DO Binary */
    send(fd, (char *)buf, 3, 0);
}

static void socket_set_nodelay(int fd)
{
    int val = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val));
}

static void tcp_chr_accept(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    struct sockaddr_in saddr;
#ifndef _WIN32
    struct sockaddr_un uaddr;
#endif
    struct sockaddr *addr;
    socklen_t len;
    int fd;

    for(;;) {
#ifndef _WIN32
	if (s->is_unix) {
	    len = sizeof(uaddr);
	    addr = (struct sockaddr *)&uaddr;
	} else
#endif
	{
	    len = sizeof(saddr);
	    addr = (struct sockaddr *)&saddr;
	}
        fd = accept(s->listen_fd, addr, &len);
        if (fd < 0 && errno != EINTR) {
            return;
        } else if (fd >= 0) {
            if (s->do_telnetopt)
                tcp_chr_telnet_init(fd);
            break;
        }
    }
    socket_set_nonblock(fd);
    if (s->do_nodelay)
        socket_set_nodelay(fd);
    s->fd = fd;
    qemu_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
    tcp_chr_connect(chr);
}

static void tcp_chr_close(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;
    if (s->fd >= 0) {
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
    }
    if (s->listen_fd >= 0) {
        qemu_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
        closesocket(s->listen_fd);
    }
    qemu_free(s);
}

static CharDriverState *qemu_chr_open_tcp(const char *host_str,
                                          int is_telnet,
					  int is_unix)
{
    CharDriverState *chr = NULL;
    TCPCharDriver *s = NULL;
    int fd = -1, offset = 0;
    int is_listen = 0;
    int is_waitconnect = 1;
    int do_nodelay = 0;
    const char *ptr;

    ptr = host_str;
    while((ptr = strchr(ptr,','))) {
        ptr++;
        if (!strncmp(ptr,"server",6)) {
            is_listen = 1;
        } else if (!strncmp(ptr,"nowait",6)) {
            is_waitconnect = 0;
        } else if (!strncmp(ptr,"nodelay",6)) {
            do_nodelay = 1;
        } else if (!strncmp(ptr,"to=",3)) {
            /* nothing, inet_listen() parses this one */;
        } else if (!strncmp(ptr,"ipv4",4)) {
            /* nothing, inet_connect() and inet_listen() parse this one */;
        } else if (!strncmp(ptr,"ipv6",4)) {
            /* nothing, inet_connect() and inet_listen() parse this one */;
        } else {
            printf("Unknown option: %s\n", ptr);
            goto fail;
        }
    }
    if (!is_listen)
        is_waitconnect = 0;

    chr = qemu_mallocz(sizeof(CharDriverState));
    s = qemu_mallocz(sizeof(TCPCharDriver));

    if (is_listen) {
        chr->filename = qemu_malloc(256);
        if (is_unix) {
            pstrcpy(chr->filename, 256, "unix:");
        } else if (is_telnet) {
            pstrcpy(chr->filename, 256, "telnet:");
        } else {
            pstrcpy(chr->filename, 256, "tcp:");
        }
        offset = strlen(chr->filename);
    }
    if (is_unix) {
        if (is_listen) {
            fd = unix_listen(host_str, chr->filename + offset, 256 - offset);
        } else {
            fd = unix_connect(host_str);
        }
    } else {
        if (is_listen) {
            fd = inet_listen(host_str, chr->filename + offset, 256 - offset,
                             SOCK_STREAM, 0);
        } else {
            fd = inet_connect(host_str, SOCK_STREAM);
        }
    }
    if (fd < 0)
        goto fail;

    if (!is_waitconnect)
        socket_set_nonblock(fd);

    s->connected = 0;
    s->fd = -1;
    s->listen_fd = -1;
    s->is_unix = is_unix;
    s->do_nodelay = do_nodelay && !is_unix;

    chr->opaque = s;
    chr->chr_write = tcp_chr_write;
    chr->chr_close = tcp_chr_close;

    if (is_listen) {
        s->listen_fd = fd;
        qemu_set_fd_handler(s->listen_fd, tcp_chr_accept, NULL, chr);
        if (is_telnet)
            s->do_telnetopt = 1;
    } else {
        s->connected = 1;
        s->fd = fd;
        socket_set_nodelay(fd);
        tcp_chr_connect(chr);
    }

    if (is_listen && is_waitconnect) {
        printf("QEMU waiting for connection on: %s\n",
               chr->filename ? chr->filename : host_str);
        tcp_chr_accept(chr);
        socket_set_nonblock(s->listen_fd);
    }

    return chr;
 fail:
    if (fd >= 0)
        closesocket(fd);
    qemu_free(s);
    qemu_free(chr);
    return NULL;
}

static CharDriverState *chr_drv_tcp_init(const char *label, const char *filename)
{
    return qemu_chr_open_tcp(filename, 0, 0);
}

static CharDriver chr_drv_tcp = {
    .name = "tcp:",
    .init = chr_drv_tcp_init,
};

static CharDriverState *chr_drv_telnet_init(const char *label, const char *filename)
{
    return qemu_chr_open_tcp(filename, 1, 0);
}

static CharDriver chr_drv_telnet = {
    .name = "telnet:",
    .init = chr_drv_telnet_init,
};

#ifndef _WIN32
static CharDriverState *chr_drv_unix_init(const char *label, const char *filename)
{
    return qemu_chr_open_tcp(filename, 0, 1);
}

static CharDriver chr_drv_unix = {
    .name = "unix:",
    .init = chr_drv_unix_init,
};
#endif

static int tcp_init(void)
{
    int ret = 0;
    ret |= qemu_chr_register_driver(&chr_drv_tcp);
    ret |= qemu_chr_register_driver(&chr_drv_telnet);
#ifndef _WIN32
    ret |= qemu_chr_register_driver(&chr_drv_unix);
#endif
    return ret;
}

char_driver_init(tcp_init);
