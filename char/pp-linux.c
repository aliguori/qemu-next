typedef struct {
    int fd;
    int mode;
} ParallelCharDriver;

static int pp_hw_mode(ParallelCharDriver *s, uint16_t mode)
{
    if (s->mode != mode) {
	int m = mode;
        if (ioctl(s->fd, PPSETMODE, &m) < 0)
            return 0;
	s->mode = mode;
    }
    return 1;
}

static int pp_ioctl(CharDriverState *chr, int cmd, void *arg)
{
    ParallelCharDriver *drv = chr->opaque;
    int fd = drv->fd;
    uint8_t b;

    switch(cmd) {
    case CHR_IOCTL_PP_READ_DATA:
        if (ioctl(fd, PPRDATA, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_DATA:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPWDATA, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_CONTROL:
        if (ioctl(fd, PPRCONTROL, &b) < 0)
            return -ENOTSUP;
	/* Linux gives only the lowest bits, and no way to know data
	   direction! For better compatibility set the fixed upper
	   bits. */
        *(uint8_t *)arg = b | 0xc0;
        break;
    case CHR_IOCTL_PP_WRITE_CONTROL:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPWCONTROL, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_STATUS:
        if (ioctl(fd, PPRSTATUS, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_DATA_DIR:
        if (ioctl(fd, PPDATADIR, (int *)arg) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_EPP_READ_ADDR:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP|IEEE1284_ADDR)) {
	    struct ParallelIOArg *parg = arg;
	    int n = read(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    case CHR_IOCTL_PP_EPP_READ:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP)) {
	    struct ParallelIOArg *parg = arg;
	    int n = read(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    case CHR_IOCTL_PP_EPP_WRITE_ADDR:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP|IEEE1284_ADDR)) {
	    struct ParallelIOArg *parg = arg;
	    int n = write(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    case CHR_IOCTL_PP_EPP_WRITE:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP)) {
	    struct ParallelIOArg *parg = arg;
	    int n = write(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static void pp_close(CharDriverState *chr)
{
    ParallelCharDriver *drv = chr->opaque;
    int fd = drv->fd;

    pp_hw_mode(drv, IEEE1284_MODE_COMPAT);
    ioctl(fd, PPRELEASE);
    close(fd);
    qemu_free(drv);
}

static CharDriverState *qemu_chr_open_pp(const char *filename)
{
    CharDriverState *chr;
    ParallelCharDriver *drv;
    int fd;

    TFR(fd = open(filename, O_RDWR));
    if (fd < 0)
        return NULL;

    if (ioctl(fd, PPCLAIM) < 0) {
        close(fd);
        return NULL;
    }

    drv = qemu_mallocz(sizeof(ParallelCharDriver));
    drv->fd = fd;
    drv->mode = IEEE1284_MODE_COMPAT;

    chr = qemu_mallocz(sizeof(CharDriverState));
    chr->chr_write = null_chr_write;
    chr->chr_ioctl = pp_ioctl;
    chr->chr_close = pp_close;
    chr->opaque = drv;

    qemu_chr_reset(chr);

    return chr;
}

static CharDriverState *chr_drv_pp_init(const char *label, const char *filename)
{
    return qemu_chr_open_pp(filename);
}

static CharDriver chr_drv_pp = {
    .name = "/dev/parport",
    .flags = CHAR_DRIVER_FULL_ARGS,
    .init = chr_drv_pp_init,
};

static int pp_init(void)
{
    return qemu_chr_register_driver(&chr_drv_pp);
}

char_driver_init(pp_init);
