static int null_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    return len;
}

static CharDriverState *qemu_chr_open_null(void)
{
    CharDriverState *chr;

    chr = qemu_mallocz(sizeof(CharDriverState));
    chr->chr_write = null_chr_write;
    return chr;
}
static CharDriverState *chr_drv_null_init(const char *label, const char *filename)
{
    return qemu_chr_open_null();
}

static CharDriver chr_drv_null = {
    .name = "null",
    .flags = CHAR_DRIVER_NO_ARGS,
    .init = chr_drv_null_init,
};

static int null_init(void)
{
    return qemu_chr_register_driver(&chr_drv_null);
}

char_driver_init(null_init);
