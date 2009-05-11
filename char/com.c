typedef struct {
    int max_size;
    HANDLE hcom, hrecv, hsend;
    OVERLAPPED orecv, osend;
    BOOL fpipe;
    DWORD len;
} WinCharState;

#define NSENDBUF 2048
#define NRECVBUF 2048
#define MAXCONNECT 1
#define NTIMEOUT 5000

static int win_chr_poll(void *opaque);
static int win_chr_pipe_poll(void *opaque);

static void win_chr_close(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;

    if (s->hsend) {
        CloseHandle(s->hsend);
        s->hsend = NULL;
    }
    if (s->hrecv) {
        CloseHandle(s->hrecv);
        s->hrecv = NULL;
    }
    if (s->hcom) {
        CloseHandle(s->hcom);
        s->hcom = NULL;
    }
    if (s->fpipe)
        qemu_del_polling_cb(win_chr_pipe_poll, chr);
    else
        qemu_del_polling_cb(win_chr_poll, chr);
}

static int win_chr_init(CharDriverState *chr, const char *filename)
{
    WinCharState *s = chr->opaque;
    COMMCONFIG comcfg;
    COMMTIMEOUTS cto = { 0, 0, 0, 0, 0};
    COMSTAT comstat;
    DWORD size;
    DWORD err;

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

    s->hcom = CreateFile(filename, GENERIC_READ|GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
    if (s->hcom == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed CreateFile (%lu)\n", GetLastError());
        s->hcom = NULL;
        goto fail;
    }

    if (!SetupComm(s->hcom, NRECVBUF, NSENDBUF)) {
        fprintf(stderr, "Failed SetupComm\n");
        goto fail;
    }

    ZeroMemory(&comcfg, sizeof(COMMCONFIG));
    size = sizeof(COMMCONFIG);
    GetDefaultCommConfig(filename, &comcfg, &size);
    comcfg.dcb.DCBlength = sizeof(DCB);
    CommConfigDialog(filename, NULL, &comcfg);

    if (!SetCommState(s->hcom, &comcfg.dcb)) {
        fprintf(stderr, "Failed SetCommState\n");
        goto fail;
    }

    if (!SetCommMask(s->hcom, EV_ERR)) {
        fprintf(stderr, "Failed SetCommMask\n");
        goto fail;
    }

    cto.ReadIntervalTimeout = MAXDWORD;
    if (!SetCommTimeouts(s->hcom, &cto)) {
        fprintf(stderr, "Failed SetCommTimeouts\n");
        goto fail;
    }

    if (!ClearCommError(s->hcom, &err, &comstat)) {
        fprintf(stderr, "Failed ClearCommError\n");
        goto fail;
    }
    qemu_add_polling_cb(win_chr_poll, chr);
    return 0;

 fail:
    win_chr_close(chr);
    return -1;
}

static int win_chr_write(CharDriverState *chr, const uint8_t *buf, int len1)
{
    WinCharState *s = chr->opaque;
    DWORD len, ret, size, err;

    len = len1;
    ZeroMemory(&s->osend, sizeof(s->osend));
    s->osend.hEvent = s->hsend;
    while (len > 0) {
        if (s->hsend)
            ret = WriteFile(s->hcom, buf, len, &size, &s->osend);
        else
            ret = WriteFile(s->hcom, buf, len, &size, NULL);
        if (!ret) {
            err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                ret = GetOverlappedResult(s->hcom, &s->osend, &size, TRUE);
                if (ret) {
                    buf += size;
                    len -= size;
                } else {
                    break;
                }
            } else {
                break;
            }
        } else {
            buf += size;
            len -= size;
        }
    }
    return len1 - len;
}

static int win_chr_read_poll(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;

    s->max_size = qemu_chr_can_read(chr);
    return s->max_size;
}

static void win_chr_readfile(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;
    int ret, err;
    uint8_t buf[1024];
    DWORD size;

    ZeroMemory(&s->orecv, sizeof(s->orecv));
    s->orecv.hEvent = s->hrecv;
    ret = ReadFile(s->hcom, buf, s->len, &size, &s->orecv);
    if (!ret) {
        err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            ret = GetOverlappedResult(s->hcom, &s->orecv, &size, TRUE);
        }
    }

    if (size > 0) {
        qemu_chr_read(chr, buf, size);
    }
}

static void win_chr_read(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;

    if (s->len > s->max_size)
        s->len = s->max_size;
    if (s->len == 0)
        return;

    win_chr_readfile(chr);
}

static int win_chr_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    WinCharState *s = chr->opaque;
    COMSTAT status;
    DWORD comerr;

    ClearCommError(s->hcom, &comerr, &status);
    if (status.cbInQue > 0) {
        s->len = status.cbInQue;
        win_chr_read_poll(chr);
        win_chr_read(chr);
        return 1;
    }
    return 0;
}

static CharDriverState *qemu_chr_open_win(const char *filename)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_mallocz(sizeof(CharDriverState));
    s = qemu_mallocz(sizeof(WinCharState));
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    chr->chr_close = win_chr_close;

    if (win_chr_init(chr, filename) < 0) {
        free(s);
        free(chr);
        return NULL;
    }
    qemu_chr_reset(chr);
    return chr;
}

static CharDriverState *chr_drv_com_init(const char *label, const char *filename)
{
    return qemu_chr_open_win(filename);
}

static CharDriver chr_drv_com = {
    .name = "COM",
    .flags = CHAR_DRIVER_FULL_ARGS,
    .init = chr_drv_com_init,
};

static int com_init(void)
{
    return qemu_chr_register_driver(&chr_drv_com);
}

char_driver_init(com_init);
