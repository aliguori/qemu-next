/***********************************************************/
/* UDP Net console */

typedef struct {
    int fd;
    struct sockaddr_in daddr;
    uint8_t buf[1024];
    int bufcnt;
    int bufptr;
    int max_size;
} NetCharDriver;

static int udp_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    NetCharDriver *s = chr->opaque;

    return sendto(s->fd, buf, len, 0,
                  (struct sockaddr *)&s->daddr, sizeof(struct sockaddr_in));
}

static int udp_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    NetCharDriver *s = chr->opaque;

    s->max_size = qemu_chr_can_read(chr);

    /* If there were any stray characters in the queue process them
     * first
     */
    while (s->max_size > 0 && s->bufptr < s->bufcnt) {
        qemu_chr_read(chr, &s->buf[s->bufptr], 1);
        s->bufptr++;
        s->max_size = qemu_chr_can_read(chr);
    }
    return s->max_size;
}

static void udp_chr_read(void *opaque)
{
    CharDriverState *chr = opaque;
    NetCharDriver *s = chr->opaque;

    if (s->max_size == 0)
        return;
    s->bufcnt = recv(s->fd, s->buf, sizeof(s->buf), 0);
    s->bufptr = s->bufcnt;
    if (s->bufcnt <= 0)
        return;

    s->bufptr = 0;
    while (s->max_size > 0 && s->bufptr < s->bufcnt) {
        qemu_chr_read(chr, &s->buf[s->bufptr], 1);
        s->bufptr++;
        s->max_size = qemu_chr_can_read(chr);
    }
}

static void udp_chr_update_read_handler(CharDriverState *chr)
{
    NetCharDriver *s = chr->opaque;

    if (s->fd >= 0) {
        qemu_set_fd_handler2(s->fd, udp_chr_read_poll,
                             udp_chr_read, NULL, chr);
    }
}

static void udp_chr_close(CharDriverState *chr)
{
    NetCharDriver *s = chr->opaque;
    if (s->fd >= 0) {
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
    }
    qemu_free(s);
}

static CharDriverState *qemu_chr_open_udp(const char *def)
{
    CharDriverState *chr = NULL;
    NetCharDriver *s = NULL;
    int fd = -1;
    struct sockaddr_in saddr;

    chr = qemu_mallocz(sizeof(CharDriverState));
    s = qemu_mallocz(sizeof(NetCharDriver));

    fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket(PF_INET, SOCK_DGRAM)");
        goto return_err;
    }

    if (parse_host_src_port(&s->daddr, &saddr, def) < 0) {
        printf("Could not parse: %s\n", def);
        goto return_err;
    }

    if (bind(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
    {
        perror("bind");
        goto return_err;
    }

    s->fd = fd;
    s->bufcnt = 0;
    s->bufptr = 0;
    chr->opaque = s;
    chr->chr_write = udp_chr_write;
    chr->chr_update_read_handler = udp_chr_update_read_handler;
    chr->chr_close = udp_chr_close;
    return chr;

return_err:
    if (chr)
        free(chr);
    if (s)
        free(s);
    if (fd >= 0)
        closesocket(fd);
    return NULL;
}

static CharDriverState *chr_drv_udp_init(const char *label, const char *filename)
{
    return qemu_chr_open_udp(filename);
}

static CharDriver chr_drv_udp = {
    .name = "udp:",
    .init = chr_drv_udp_init,
};

static int udp_init(void)
{
    return qemu_chr_register_driver(&chr_drv_udp);
}

char_driver_init(udp_init);
