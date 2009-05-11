/* MUX driver for serial I/O splitting */
static int term_timestamps;
static int64_t term_timestamps_start;
#define MAX_MUX 4
#define MUX_BUFFER_SIZE 32	/* Must be a power of 2.  */
#define MUX_BUFFER_MASK (MUX_BUFFER_SIZE - 1)
typedef struct {
    IOCanRWHandler *chr_can_read[MAX_MUX];
    IOReadHandler *chr_read[MAX_MUX];
    IOEventHandler *chr_event[MAX_MUX];
    void *ext_opaque[MAX_MUX];
    CharDriverState *drv;
    int mux_cnt;
    int term_got_escape;
    int max_size;
    /* Intermediate input buffer allows to catch escape sequences even if the
       currently active device is not accepting any input - but only until it
       is full as well. */
    unsigned char buffer[MAX_MUX][MUX_BUFFER_SIZE];
    int prod[MAX_MUX];
    int cons[MAX_MUX];
} MuxDriver;


static int mux_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    MuxDriver *d = chr->opaque;
    int ret;
    if (!term_timestamps) {
        ret = d->drv->chr_write(d->drv, buf, len);
    } else {
        int i;

        ret = 0;
        for(i = 0; i < len; i++) {
            ret += d->drv->chr_write(d->drv, buf+i, 1);
            if (buf[i] == '\n') {
                char buf1[64];
                int64_t ti;
                int secs;

                ti = qemu_get_clock(rt_clock);
                if (term_timestamps_start == -1)
                    term_timestamps_start = ti;
                ti -= term_timestamps_start;
                secs = ti / 1000;
                snprintf(buf1, sizeof(buf1),
                         "[%02d:%02d:%02d.%03d] ",
                         secs / 3600,
                         (secs / 60) % 60,
                         secs % 60,
                         (int)(ti % 1000));
                d->drv->chr_write(d->drv, (uint8_t *)buf1, strlen(buf1));
            }
        }
    }
    return ret;
}

static const char * const mux_help[] = {
    "% h    print this help\n\r",
    "% x    exit emulator\n\r",
    "% s    save disk data back to file (if -snapshot)\n\r",
    "% t    toggle console timestamps\n\r"
    "% b    send break (magic sysrq)\n\r",
    "% c    switch between console and monitor\n\r",
    "% %  sends %\n\r",
    NULL
};

int term_escape_char = 0x01; /* ctrl-a is used for escape */
static void mux_print_help(CharDriverState *chr)
{
    int i, j;
    char ebuf[15] = "Escape-Char";
    char cbuf[50] = "\n\r";

    if (term_escape_char > 0 && term_escape_char < 26) {
        snprintf(cbuf, sizeof(cbuf), "\n\r");
        snprintf(ebuf, sizeof(ebuf), "C-%c", term_escape_char - 1 + 'a');
    } else {
        snprintf(cbuf, sizeof(cbuf),
                 "\n\rEscape-Char set to Ascii: 0x%02x\n\r\n\r",
                 term_escape_char);
    }
    chr->chr_write(chr, (uint8_t *)cbuf, strlen(cbuf));
    for (i = 0; mux_help[i] != NULL; i++) {
        for (j=0; mux_help[i][j] != '\0'; j++) {
            if (mux_help[i][j] == '%')
                chr->chr_write(chr, (uint8_t *)ebuf, strlen(ebuf));
            else
                chr->chr_write(chr, (uint8_t *)&mux_help[i][j], 1);
        }
    }
}

static void mux_chr_send_event(MuxDriver *d, int mux_nr, int event)
{
    if (d->chr_event[mux_nr])
        d->chr_event[mux_nr](d->ext_opaque[mux_nr], event);
}

static int mux_proc_byte(CharDriverState *chr, MuxDriver *d, int ch)
{
    if (d->term_got_escape) {
        d->term_got_escape = 0;
        if (ch == term_escape_char)
            goto send_char;
        switch(ch) {
        case '?':
        case 'h':
            mux_print_help(chr);
            break;
        case 'x':
            {
                 const char *term =  "QEMU: Terminated\n\r";
                 chr->chr_write(chr,(uint8_t *)term,strlen(term));
                 exit(0);
                 break;
            }
        case 's':
            {
                int i;
                for (i = 0; i < nb_drives; i++) {
                        bdrv_commit(drives_table[i].bdrv);
                }
            }
            break;
        case 'b':
            qemu_chr_event(chr, CHR_EVENT_BREAK);
            break;
        case 'c':
            /* Switch to the next registered device */
            mux_chr_send_event(d, chr->focus, CHR_EVENT_MUX_OUT);
            chr->focus++;
            if (chr->focus >= d->mux_cnt)
                chr->focus = 0;
            mux_chr_send_event(d, chr->focus, CHR_EVENT_MUX_IN);
            break;
       case 't':
           term_timestamps = !term_timestamps;
           term_timestamps_start = -1;
           break;
        }
    } else if (ch == term_escape_char) {
        d->term_got_escape = 1;
    } else {
    send_char:
        return 1;
    }
    return 0;
}

static void mux_chr_accept_input(CharDriverState *chr)
{
    int m = chr->focus;
    MuxDriver *d = chr->opaque;

    while (d->prod[m] != d->cons[m] &&
           d->chr_can_read[m] &&
           d->chr_can_read[m](d->ext_opaque[m])) {
        d->chr_read[m](d->ext_opaque[m],
                       &d->buffer[m][d->cons[m]++ & MUX_BUFFER_MASK], 1);
    }
}

static int mux_chr_can_read(void *opaque)
{
    CharDriverState *chr = opaque;
    MuxDriver *d = chr->opaque;
    int m = chr->focus;

    if ((d->prod[m] - d->cons[m]) < MUX_BUFFER_SIZE)
        return 1;
    if (d->chr_can_read[m])
        return d->chr_can_read[m](d->ext_opaque[m]);
    return 0;
}

static void mux_chr_read(void *opaque, const uint8_t *buf, int size)
{
    CharDriverState *chr = opaque;
    MuxDriver *d = chr->opaque;
    int m = chr->focus;
    int i;

    mux_chr_accept_input (opaque);

    for(i = 0; i < size; i++)
        if (mux_proc_byte(chr, d, buf[i])) {
            if (d->prod[m] == d->cons[m] &&
                d->chr_can_read[m] &&
                d->chr_can_read[m](d->ext_opaque[m]))
                d->chr_read[m](d->ext_opaque[m], &buf[i], 1);
            else
                d->buffer[m][d->prod[m]++ & MUX_BUFFER_MASK] = buf[i];
        }
}

static void mux_chr_event(void *opaque, int event)
{
    CharDriverState *chr = opaque;
    MuxDriver *d = chr->opaque;
    int i;

    /* Send the event to all registered listeners */
    for (i = 0; i < d->mux_cnt; i++)
        mux_chr_send_event(d, i, event);
}

static void mux_chr_update_read_handler(CharDriverState *chr)
{
    MuxDriver *d = chr->opaque;

    if (d->mux_cnt >= MAX_MUX) {
        fprintf(stderr, "Cannot add I/O handlers, MUX array is full\n");
        return;
    }
    d->ext_opaque[d->mux_cnt] = chr->handler_opaque;
    d->chr_can_read[d->mux_cnt] = chr->chr_can_read;
    d->chr_read[d->mux_cnt] = chr->chr_read;
    d->chr_event[d->mux_cnt] = chr->chr_event;
    /* Fix up the real driver with mux routines */
    if (d->mux_cnt == 0) {
        qemu_chr_add_handlers(d->drv, mux_chr_can_read, mux_chr_read,
                              mux_chr_event, chr);
    }
    chr->focus = d->mux_cnt;
    d->mux_cnt++;
}

static CharDriverState *qemu_chr_open_mux(CharDriverState *drv)
{
    CharDriverState *chr;
    MuxDriver *d;

    chr = qemu_mallocz(sizeof(CharDriverState));
    d = qemu_mallocz(sizeof(MuxDriver));

    chr->opaque = d;
    d->drv = drv;
    chr->focus = -1;
    chr->chr_write = mux_chr_write;
    chr->chr_update_read_handler = mux_chr_update_read_handler;
    chr->chr_accept_input = mux_chr_accept_input;
    return chr;
}

static CharDriverState *chr_drv_mon_init(const char *label, const char *filename)
{
    CharDriverState *chr;

    chr = qemu_chr_open(label, filename, NULL);
    if (chr) {
        chr = qemu_chr_open_mux(chr);
        monitor_init(chr, MONITOR_USE_READLINE);
    } else {
        printf("Unable to open driver: %s\n", filename);
    }

    return chr;
}

static CharDriver chr_drv_mon = {
    .name = "mon:",
    .init = chr_drv_mon_init,
};

static int mon_init(void)
{
    return qemu_chr_register_driver(&chr_drv_mon);
}

char_driver_init(mon_init);
