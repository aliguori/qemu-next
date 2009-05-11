/* for STDIO, we handle the case where several clients use it
   (nographic mode) */

#define TERM_FIFO_MAX_SIZE 1

static uint8_t term_fifo[TERM_FIFO_MAX_SIZE];
static int term_fifo_size;

static int stdio_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;

    /* try to flush the queue if needed */
    if (term_fifo_size != 0 && qemu_chr_can_read(chr) > 0) {
        qemu_chr_read(chr, term_fifo, 1);
        term_fifo_size = 0;
    }
    /* see if we can absorb more chars */
    if (term_fifo_size == 0)
        return 1;
    else
        return 0;
}

static void stdio_read(void *opaque)
{
    int size;
    uint8_t buf[1];
    CharDriverState *chr = opaque;

    size = read(0, buf, 1);
    if (size == 0) {
        /* stdin has been closed. Remove it from the active list.  */
        qemu_set_fd_handler2(0, NULL, NULL, NULL, NULL);
        return;
    }
    if (size > 0) {
        if (qemu_chr_can_read(chr) > 0) {
            qemu_chr_read(chr, buf, 1);
        } else if (term_fifo_size == 0) {
            term_fifo[term_fifo_size++] = buf[0];
        }
    }
}

/* init terminal so that we can grab keys */
static struct termios oldtty;
static int old_fd0_flags;
static int term_atexit_done;

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
    fcntl(0, F_SETFL, old_fd0_flags);
}

static void term_init(void)
{
    struct termios tty;

    tcgetattr (0, &tty);
    oldtty = tty;
    old_fd0_flags = fcntl(0, F_GETFL);

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    /* if graphical mode, we allow Ctrl-C handling */
    if (nographic)
        tty.c_lflag &= ~ISIG;
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr (0, TCSANOW, &tty);

    if (!term_atexit_done++)
        atexit(term_exit);

    fcntl(0, F_SETFL, O_NONBLOCK);
}

#define STDIO_MAX_CLIENTS 1
static int stdio_nb_clients = 0;

static void qemu_chr_close_stdio(struct CharDriverState *chr)
{
    term_exit();
    stdio_nb_clients--;
    qemu_set_fd_handler2(0, NULL, NULL, NULL, NULL);
    fd_chr_close(chr);
}

static CharDriverState *qemu_chr_open_stdio(void)
{
    CharDriverState *chr;

    if (stdio_nb_clients >= STDIO_MAX_CLIENTS)
        return NULL;
    chr = qemu_chr_open_fd(0, 1);
    chr->chr_close = qemu_chr_close_stdio;
    qemu_set_fd_handler2(0, stdio_read_poll, stdio_read, NULL, chr);
    stdio_nb_clients++;
    term_init();

    return chr;
}

static CharDriverState *chr_drv_stdio_init(const char *label, const char *filename)
{
    return qemu_chr_open_stdio();
}

static CharDriver chr_drv_stdio = {
    .name = "stdio",
    .flags = CHAR_DRIVER_NO_ARGS,
    .init = chr_drv_stdio_init,
};

static int stdio_init(void)
{
    return qemu_chr_register_driver(&chr_drv_stdio);
}

char_driver_init(stdio_init);
