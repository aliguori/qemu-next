static int pp_ioctl(CharDriverState *chr, int cmd, void *arg)
{
    int fd = (int)chr->opaque;
    uint8_t b;

    switch(cmd) {
    case CHR_IOCTL_PP_READ_DATA:
        if (ioctl(fd, PPIGDATA, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_DATA:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPISDATA, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_CONTROL:
        if (ioctl(fd, PPIGCTRL, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_CONTROL:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPISCTRL, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_STATUS:
        if (ioctl(fd, PPIGSTATUS, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static CharDriverState *qemu_chr_open_pp(const char *filename)
{
    CharDriverState *chr;
    int fd;

    fd = open(filename, O_RDWR);
    if (fd < 0)
        return NULL;

    chr = qemu_mallocz(sizeof(CharDriverState));
    chr->opaque = (void *)fd;
    chr->chr_write = null_chr_write;
    chr->chr_ioctl = pp_ioctl;
    return chr;
}

static CharDriverState *chr_drv_pp_init(const char *label, const char *filename)
{
    return qemu_chr_open_pp(filename);
}

static CharDriver chr_drv_pp = {
    .name = "/dev/ppi",
    .flags = CHAR_DRIVER_FULL_ARGS,
    .init = chr_drv_pp_init,
};

static int pp_init(void)
{
    return qemu_chr_register_driver(&chr_drv_pp);
}

char_driver_init(pp_init);
