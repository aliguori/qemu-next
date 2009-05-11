static CharDriverState *chr_drv_vc_init(const char *label, const char *filename)
{
    return text_console_init(0);
}

static CharDriver chr_drv_vc = {
    .name = "vc",
    .flags = CHAR_DRIVER_NO_ARGS,
    .init = chr_drv_vc_init,
};

static CharDriverState *chr_drv_vc_init_with_args(const char *label, const char *filename)
{
    return text_console_init(filename);
}

static CharDriver chr_drv_vc_args = {
    .name = "vc:",
    .init = chr_drv_vc_init_with_args,
};

static int vc_init(void)
{
    int ret = 0;

    ret |= qemu_chr_register_driver(&chr_drv_vc);
    ret |= qemu_chr_register_driver(&chr_drv_vc_args);

    return ret;
}

char_driver_init(vc_init);
