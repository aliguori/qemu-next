typedef struct {
    int fd_in, fd_out;
    int max_size;
} FDCharDriver;

static int fd_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    FDCharDriver *s = chr->opaque;
    return send_all(s->fd_out, buf, len);
}

static int fd_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    FDCharDriver *s = chr->opaque;

    s->max_size = qemu_chr_can_read(chr);
    return s->max_size;
}

static void fd_chr_read(void *opaque)
{
    CharDriverState *chr = opaque;
    FDCharDriver *s = chr->opaque;
    int size, len;
    uint8_t buf[1024];

    len = sizeof(buf);
    if (len > s->max_size)
        len = s->max_size;
    if (len == 0)
        return;
    size = read(s->fd_in, buf, len);
    if (size == 0) {
        /* FD has been closed. Remove it from the active list.  */
        qemu_set_fd_handler2(s->fd_in, NULL, NULL, NULL, NULL);
        return;
    }
    if (size > 0) {
        qemu_chr_read(chr, buf, size);
    }
}

static void fd_chr_update_read_handler(CharDriverState *chr)
{
    FDCharDriver *s = chr->opaque;

    if (s->fd_in >= 0) {
        if (nographic && s->fd_in == 0) {
        } else {
            qemu_set_fd_handler2(s->fd_in, fd_chr_read_poll,
                                 fd_chr_read, NULL, chr);
        }
    }
}

static void fd_chr_close(struct CharDriverState *chr)
{
    FDCharDriver *s = chr->opaque;

    if (s->fd_in >= 0) {
        if (nographic && s->fd_in == 0) {
        } else {
            qemu_set_fd_handler2(s->fd_in, NULL, NULL, NULL, NULL);
        }
    }

    qemu_free(s);
}

/* open a character device to a unix fd */
CharDriverState *qemu_chr_open_fd(int fd_in, int fd_out)
{
    CharDriverState *chr;
    FDCharDriver *s;

    chr = qemu_mallocz(sizeof(CharDriverState));
    s = qemu_mallocz(sizeof(FDCharDriver));
    s->fd_in = fd_in;
    s->fd_out = fd_out;
    chr->opaque = s;
    chr->chr_write = fd_chr_write;
    chr->chr_update_read_handler = fd_chr_update_read_handler;
    chr->chr_close = fd_chr_close;

    qemu_chr_reset(chr);

    return chr;
}
