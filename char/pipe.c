static CharDriverState *qemu_chr_open_pipe(const char *filename)
{
    int fd_in, fd_out;
    char filename_in[256], filename_out[256];

    snprintf(filename_in, 256, "%s.in", filename);
    snprintf(filename_out, 256, "%s.out", filename);
    TFR(fd_in = open(filename_in, O_RDWR | O_BINARY));
    TFR(fd_out = open(filename_out, O_RDWR | O_BINARY));
    if (fd_in < 0 || fd_out < 0) {
	if (fd_in >= 0)
	    close(fd_in);
	if (fd_out >= 0)
	    close(fd_out);
        TFR(fd_in = fd_out = open(filename, O_RDWR | O_BINARY));
        if (fd_in < 0)
            return NULL;
    }
    return qemu_chr_open_fd(fd_in, fd_out);
}

static CharDriverState *chr_drv_pipe_init(const char *label, const char *filename)
{
    return qemu_chr_open_pipe(filename);
}

static CharDriver chr_drv_pipe = {
    .name = "pipe:",
    .init = chr_drv_pipe_init,
};

static int pipe_init(void)
{
    return qemu_chr_register_driver(&chr_drv_pipe);
}

char_driver_init(pipe_init);
