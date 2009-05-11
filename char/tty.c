static void tty_serial_init(int fd, int speed,
                            int parity, int data_bits, int stop_bits)
{
    struct termios tty;
    speed_t spd;

#if 0
    printf("tty_serial_init: speed=%d parity=%c data=%d stop=%d\n",
           speed, parity, data_bits, stop_bits);
#endif
    tcgetattr (fd, &tty);

#define MARGIN 1.1
    if (speed <= 50 * MARGIN)
        spd = B50;
    else if (speed <= 75 * MARGIN)
        spd = B75;
    else if (speed <= 300 * MARGIN)
        spd = B300;
    else if (speed <= 600 * MARGIN)
        spd = B600;
    else if (speed <= 1200 * MARGIN)
        spd = B1200;
    else if (speed <= 2400 * MARGIN)
        spd = B2400;
    else if (speed <= 4800 * MARGIN)
        spd = B4800;
    else if (speed <= 9600 * MARGIN)
        spd = B9600;
    else if (speed <= 19200 * MARGIN)
        spd = B19200;
    else if (speed <= 38400 * MARGIN)
        spd = B38400;
    else if (speed <= 57600 * MARGIN)
        spd = B57600;
    else if (speed <= 115200 * MARGIN)
        spd = B115200;
    else
        spd = B115200;

    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);
    tty.c_cflag &= ~(CSIZE|PARENB|PARODD|CRTSCTS|CSTOPB);
    switch(data_bits) {
    default:
    case 8:
        tty.c_cflag |= CS8;
        break;
    case 7:
        tty.c_cflag |= CS7;
        break;
    case 6:
        tty.c_cflag |= CS6;
        break;
    case 5:
        tty.c_cflag |= CS5;
        break;
    }
    switch(parity) {
    default:
    case 'N':
        break;
    case 'E':
        tty.c_cflag |= PARENB;
        break;
    case 'O':
        tty.c_cflag |= PARENB | PARODD;
        break;
    }
    if (stop_bits == 2)
        tty.c_cflag |= CSTOPB;

    tcsetattr (fd, TCSANOW, &tty);
}

static int tty_serial_ioctl(CharDriverState *chr, int cmd, void *arg)
{
    FDCharDriver *s = chr->opaque;

    switch(cmd) {
    case CHR_IOCTL_SERIAL_SET_PARAMS:
        {
            QEMUSerialSetParams *ssp = arg;
            tty_serial_init(s->fd_in, ssp->speed, ssp->parity,
                            ssp->data_bits, ssp->stop_bits);
        }
        break;
    case CHR_IOCTL_SERIAL_SET_BREAK:
        {
            int enable = *(int *)arg;
            if (enable)
                tcsendbreak(s->fd_in, 1);
        }
        break;
    case CHR_IOCTL_SERIAL_GET_TIOCM:
        {
            int sarg = 0;
            int *targ = (int *)arg;
            ioctl(s->fd_in, TIOCMGET, &sarg);
            *targ = 0;
            if (sarg & TIOCM_CTS)
                *targ |= CHR_TIOCM_CTS;
            if (sarg & TIOCM_CAR)
                *targ |= CHR_TIOCM_CAR;
            if (sarg & TIOCM_DSR)
                *targ |= CHR_TIOCM_DSR;
            if (sarg & TIOCM_RI)
                *targ |= CHR_TIOCM_RI;
            if (sarg & TIOCM_DTR)
                *targ |= CHR_TIOCM_DTR;
            if (sarg & TIOCM_RTS)
                *targ |= CHR_TIOCM_RTS;
        }
        break;
    case CHR_IOCTL_SERIAL_SET_TIOCM:
        {
            int sarg = *(int *)arg;
            int targ = 0;
            ioctl(s->fd_in, TIOCMGET, &targ);
            targ &= ~(CHR_TIOCM_CTS | CHR_TIOCM_CAR | CHR_TIOCM_DSR
                     | CHR_TIOCM_RI | CHR_TIOCM_DTR | CHR_TIOCM_RTS);
            if (sarg & CHR_TIOCM_CTS)
                targ |= TIOCM_CTS;
            if (sarg & CHR_TIOCM_CAR)
                targ |= TIOCM_CAR;
            if (sarg & CHR_TIOCM_DSR)
                targ |= TIOCM_DSR;
            if (sarg & CHR_TIOCM_RI)
                targ |= TIOCM_RI;
            if (sarg & CHR_TIOCM_DTR)
                targ |= TIOCM_DTR;
            if (sarg & CHR_TIOCM_RTS)
                targ |= TIOCM_RTS;
            ioctl(s->fd_in, TIOCMSET, &targ);
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static CharDriverState *qemu_chr_open_tty(const char *filename)
{
    CharDriverState *chr;
    int fd;

    TFR(fd = open(filename, O_RDWR | O_NONBLOCK));
    tty_serial_init(fd, 115200, 'N', 8, 1);
    chr = qemu_chr_open_fd(fd, fd);
    if (!chr) {
        close(fd);
        return NULL;
    }
    chr->chr_ioctl = tty_serial_ioctl;
    qemu_chr_reset(chr);
    return chr;
}

static CharDriverState *chr_drv_tty_init(const char *label, const char *filename)
{
    return qemu_chr_open_tty(filename);
}

/* FIXME need priority for this */
static CharDriver chr_drv_tty = {
    .name = "/dev/",
    .flags = CHAR_DRIVER_FULL_ARGS,
    .init = chr_drv_tty_init,
};

static int tty_init(void)
{
    return qemu_chr_driver_register(&chr_drv_tty);
}

char_driver_init(tty_init);
