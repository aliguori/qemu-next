#include "chrdrv.h"

int char_driver_write(CharDriver *s, const uint8_t *buf, int len)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    return cdc->write(s, buf, len);
}

int char_driver_ioctl(CharDriver *s, int cmd, void *arg)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    return cdc->ioctl(s, cmd, arg);
}

int char_driver_get_msgfd(CharDriver *s)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    return cdc->get_msgfd(s);
}

void char_driver_send_event(CharDriver *s, int event)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    cdc->send_event(s, event);
}

void char_driver_close(CharDriver *s)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    cdc->close(s);
}

void char_driver_accept_input(CharDriver *s)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    cdc->accept_input(s);
}

void char_driver_set_echo(CharDriver *s, bool echo)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    cdc->set_echo(s, echo);
}

void char_driver_guest_open(CharDriver *s)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    cdc->guest_open(s);
}

void char_driver_guest_close(CharDriver *s)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    cdc->guest_close(s);
}

static int char_driver_def_write(CharDriver *s, const uint8_t *buf, int len)
{
    return -ENOTSUP;
}

static void char_driver_def_update_read_handler(CharDriver *s)
{
}

static int char_driver_def_ioctl(CharDriver *s, int cmd, void *arg)
{
    return -ENOTSUP;
}

static int char_driver_def_get_msgfd(CharDriver *s)
{
    return -1;
}

static void char_driver_def_send_event(CharDriver *chr, int event)
{
}

static void char_driver_def_close(CharDriver *chr)
{
    char_driver_send_event(chr, CHR_EVENT_CLOSED);
}

static void char_driver_def_accept_input(CharDriver *chr)
{
}

static void char_driver_def_set_echo(CharDriver *chr, bool echo)
{
}

static void char_driver_def_guest_open(CharDriver *chr)
{
}

static void char_driver_def_guest_close(CharDriver *chr)
{
}

static void char_driver_def_open(CharDriver *chr, Error **errp)
{
}

static void char_driver_realize(Plug *plug)
{
    CharDriver *chr = CHAR_DRIVER(plug);
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(chr);

    cdc->open(chr, NULL);
}

int char_driver_can_read(CharDriver *chr)
{
    if (!chr->chr_can_read) {
        return 1024;
    }

    return chr->chr_can_read(chr->handler_opaque);
}

void char_driver_read(CharDriver *chr, uint8_t *buf, int len)
{
    if (!chr->chr_read) {
        return;
    }

    chr->chr_read(chr->handler_opaque, buf, len);
}

void char_driver_event(CharDriver *chr, int event)
{
    /* Keep track if the char device is open */
    switch (event) {
    case CHR_EVENT_OPENED:
        chr->opened = 1;
        break;
    case CHR_EVENT_CLOSED:
        chr->opened = 0;
        break;
    }

    if (!chr->chr_event) {
        return;
    }

    chr->chr_event(chr->handler_opaque, event);
}

static void char_driver_class_init(TypeClass *class)
{
    PlugClass *pc = PLUG_CLASS(class);
    CharDriverClass *cdc = CHAR_DRIVER_CLASS(class);

    pc->realize = char_driver_realize;
    cdc->write = char_driver_def_write;
    cdc->ioctl = char_driver_def_ioctl;
    cdc->get_msgfd = char_driver_def_get_msgfd;
    cdc->send_event = char_driver_def_send_event;
    cdc->close = char_driver_def_close;
    cdc->accept_input = char_driver_def_accept_input;
    cdc->set_echo = char_driver_def_set_echo;
    cdc->guest_open = char_driver_def_guest_open;
    cdc->guest_close = char_driver_def_guest_close;
    cdc->open = char_driver_def_open;
    cdc->update_read_handler = char_driver_def_update_read_handler;

}

static void char_driver_generic_open(CharDriver *s)
{
    char_driver_event(s, CHR_EVENT_OPENED);
}

void char_driver_add_handlers(CharDriver *s,
                              IOCanReadHandler *fd_can_read,
                              IOReadHandler *fd_read,
                              IOEventHandler *fd_event,
                              void *opaque)
{
    CharDriverClass *cdc = CHAR_DRIVER_GET_CLASS(s);

    if (!opaque && !fd_can_read && !fd_read && !fd_event) {
        /* chr driver being released. */
        ++s->avail_connections;
    }
    s->chr_can_read = fd_can_read;
    s->chr_read = fd_read;
    s->chr_event = fd_event;
    s->handler_opaque = opaque;
    if (cdc->update_read_handler) {
        cdc->update_read_handler(s);
    }

    /* We're connecting to an already opened device, so let's make sure we
       also get the open event */
    if (s->opened) {
        char_driver_generic_open(s);
    }
}

static TypeInfo chrdrv_type_info = {
    .name = TYPE_CHAR_DRIVER,
    .parent = TYPE_PLUG,
    .instance_size = sizeof(CharDriver),
    .class_size = sizeof(CharDriverClass),
    .class_init = char_driver_class_init,
};

static void register_backends(void)
{
    type_register_static(&chrdrv_type_info);
}

device_init(register_backends);
