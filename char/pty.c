#ifdef __sun__
/* Once Solaris has openpty(), this is going to be removed. */
static int openpty(int *amaster, int *aslave, char *name,
                   struct termios *termp, struct winsize *winp)
{
        const char *slave;
        int mfd = -1, sfd = -1;

        *amaster = *aslave = -1;

        mfd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
        if (mfd < 0)
                goto err;

        if (grantpt(mfd) == -1 || unlockpt(mfd) == -1)
                goto err;

        if ((slave = ptsname(mfd)) == NULL)
                goto err;

        if ((sfd = open(slave, O_RDONLY | O_NOCTTY)) == -1)
                goto err;

        if (ioctl(sfd, I_PUSH, "ptem") == -1 ||
            (termp != NULL && tcgetattr(sfd, termp) < 0))
                goto err;

        if (amaster)
                *amaster = mfd;
        if (aslave)
                *aslave = sfd;
        if (winp)
                ioctl(sfd, TIOCSWINSZ, winp);

        return 0;

err:
        if (sfd != -1)
                close(sfd);
        close(mfd);
        return -1;
}

static void cfmakeraw (struct termios *termios_p)
{
        termios_p->c_iflag &=
                ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
        termios_p->c_oflag &= ~OPOST;
        termios_p->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
        termios_p->c_cflag &= ~(CSIZE|PARENB);
        termios_p->c_cflag |= CS8;

        termios_p->c_cc[VMIN] = 0;
        termios_p->c_cc[VTIME] = 0;
}
#endif

typedef struct {
    int fd;
    int connected;
    int polling;
    int read_bytes;
    QEMUTimer *timer;
} PtyCharDriver;

static void pty_chr_update_read_handler(CharDriverState *chr);
static void pty_chr_state(CharDriverState *chr, int connected);

static int pty_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    PtyCharDriver *s = chr->opaque;

    if (!s->connected) {
        /* guest sends data, check for (re-)connect */
        pty_chr_update_read_handler(chr);
        return 0;
    }
    return send_all(s->fd, buf, len);
}

static int pty_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;

    s->read_bytes = qemu_chr_can_read(chr);
    return s->read_bytes;
}

static void pty_chr_read(void *opaque)
{
    CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;
    int size, len;
    uint8_t buf[1024];

    len = sizeof(buf);
    if (len > s->read_bytes)
        len = s->read_bytes;
    if (len == 0)
        return;
    size = read(s->fd, buf, len);
    if ((size == -1 && errno == EIO) ||
        (size == 0)) {
        pty_chr_state(chr, 0);
        return;
    }
    if (size > 0) {
        pty_chr_state(chr, 1);
        qemu_chr_read(chr, buf, size);
    }
}

static void pty_chr_update_read_handler(CharDriverState *chr)
{
    PtyCharDriver *s = chr->opaque;

    qemu_set_fd_handler2(s->fd, pty_chr_read_poll,
                         pty_chr_read, NULL, chr);
    s->polling = 1;
    /*
     * Short timeout here: just need wait long enougth that qemu makes
     * it through the poll loop once.  When reconnected we want a
     * short timeout so we notice it almost instantly.  Otherwise
     * read() gives us -EIO instantly, making pty_chr_state() reset the
     * timeout to the normal (much longer) poll interval before the
     * timer triggers.
     */
    qemu_mod_timer(s->timer, qemu_get_clock(rt_clock) + 10);
}

static void pty_chr_state(CharDriverState *chr, int connected)
{
    PtyCharDriver *s = chr->opaque;

    if (!connected) {
        qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
        s->connected = 0;
        s->polling = 0;
        /* (re-)connect poll interval for idle guests: once per second.
         * We check more frequently in case the guests sends data to
         * the virtual device linked to our pty. */
        qemu_mod_timer(s->timer, qemu_get_clock(rt_clock) + 1000);
    } else {
        if (!s->connected)
            qemu_chr_reset(chr);
        s->connected = 1;
    }
}

static void pty_chr_timer(void *opaque)
{
    struct CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;

    if (s->connected)
        return;
    if (s->polling) {
        /* If we arrive here without polling being cleared due
         * read returning -EIO, then we are (re-)connected */
        pty_chr_state(chr, 1);
        return;
    }

    /* Next poll ... */
    pty_chr_update_read_handler(chr);
}

static void pty_chr_close(struct CharDriverState *chr)
{
    PtyCharDriver *s = chr->opaque;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    close(s->fd);
    qemu_del_timer(s->timer);
    qemu_free_timer(s->timer);
    qemu_free(s);
}

static CharDriverState *qemu_chr_open_pty(void)
{
    CharDriverState *chr;
    PtyCharDriver *s;
    struct termios tty;
    int slave_fd, len;
#if defined(__OpenBSD__) || defined(__DragonFly__)
    char pty_name[PATH_MAX];
#define q_ptsname(x) pty_name
#else
    char *pty_name = NULL;
#define q_ptsname(x) ptsname(x)
#endif

    chr = qemu_mallocz(sizeof(CharDriverState));
    s = qemu_mallocz(sizeof(PtyCharDriver));

    if (openpty(&s->fd, &slave_fd, pty_name, NULL, NULL) < 0) {
        return NULL;
    }

    /* Set raw attributes on the pty. */
    tcgetattr(slave_fd, &tty);
    cfmakeraw(&tty);
    tcsetattr(slave_fd, TCSAFLUSH, &tty);
    close(slave_fd);

    len = strlen(q_ptsname(s->fd)) + 5;
    chr->filename = qemu_malloc(len);
    snprintf(chr->filename, len, "pty:%s", q_ptsname(s->fd));
    fprintf(stderr, "char device redirected to %s\n", q_ptsname(s->fd));

    chr->opaque = s;
    chr->chr_write = pty_chr_write;
    chr->chr_update_read_handler = pty_chr_update_read_handler;
    chr->chr_close = pty_chr_close;

    s->timer = qemu_new_timer(rt_clock, pty_chr_timer, chr);

    return chr;
}

#else  /* ! __linux__ && ! __sun__ */
static CharDriverState *qemu_chr_open_pty(void)
{
    return NULL;
}
#endif /* __linux__ || __sun__ */

static CharDriverState *chr_drv_pty_init(const char *label, const char *filename)
{
    return qemu_chr_open_pty();
}

static CharDriver chr_drv_pty = {
    .name = "pty",
    .flags = CHAR_DRIVER_NO_ARGS,
    .init = chr_drv_pty_init,
};

static int pty_init(void)
{
    return qemu_chr_register_driver(&chr_drv_pty);
}

char_driver_init(pty_init);
