static CharDriverState *qemu_chr_open_file_out(const char *file_out)
{
    int fd_out;

    TFR(fd_out = open(file_out, O_WRONLY | O_TRUNC | O_CREAT | O_BINARY, 0666));
    if (fd_out < 0)
        return NULL;
    return qemu_chr_open_fd(-1, fd_out);
}

static CharDriverState *chr_drv_file_init(const char *label, const char *filename)
{
    return qemu_chr_open_file_out(filename);
}

static CharDriver chr_drv_file = {
    .name = "file:",
    .init = chr_drv_file_init,
};

static int file_init(void)
{
    return qemu_chr_register_driver(&chr_drv_file);
}

char_driver_init(file_init);
