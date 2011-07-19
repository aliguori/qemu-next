#ifndef QEMU_CHAR_DRIVER
#define QEMU_CHAR_DRIVER

#include "qemu/plug.h"
// Temporary
#include "qemu-char.h"

typedef struct CharDriver
{
    Plug parent;

    int avail_connections;
    int opened;
    IOCanReadHandler *chr_can_read;
    IOReadHandler *chr_read;
    IOEventHandler *chr_event;
    void *handler_opaque;
} CharDriver;

typedef struct CharDriverClass
{
    PlugClass parent_class;

    /* Public */
    int (*write)(CharDriver *s, const uint8_t *buf, int len);
    int (*ioctl)(CharDriver *s, int cmd, void *arg);
    int (*get_msgfd)(CharDriver *s);
    void (*send_event)(CharDriver *chr, int event);
    void (*close)(CharDriver *chr);
    void (*accept_input)(CharDriver *chr);
    void (*set_echo)(CharDriver *chr, bool echo);
    void (*guest_open)(CharDriver *chr);
    void (*guest_close)(CharDriver *chr);

    /* Protected */
    void (*open)(CharDriver *s, Error **errp);
    void (*update_read_handler)(CharDriver *s);
} CharDriverClass;

#define TYPE_CHAR_DRIVER "char-driver"
#define CHAR_DRIVER(obj) TYPE_CHECK(CharDriver, obj, TYPE_CHAR_DRIVER)
#define CHAR_DRIVER_CLASS(class) \
    TYPE_CLASS_CHECK(CharDriverClass, class, TYPE_CHAR_DRIVER)
#define CHAR_DRIVER_GET_CLASS(obj) \
    TYPE_GET_CLASS(CharDriverClass, obj, TYPE_CHAR_DRIVER)

int char_driver_write(CharDriver *s, const uint8_t *buf, int len);
int char_driver_ioctl(CharDriver *s, int cmd, void *arg);
int char_driver_get_msgfd(CharDriver *s);
void char_driver_send_event(CharDriver *chr, int event);
void char_driver_close(CharDriver *chr);
void char_driver_accept_input(CharDriver *chr);
void char_driver_set_echo(CharDriver *chr, bool echo);
void char_driver_guest_open(CharDriver *chr);
void char_driver_guest_close(CharDriver *chr);

int char_driver_can_read(CharDriver *chr);
void char_driver_read(CharDriver *chr, uint8_t *buf, int len);
void char_driver_event(CharDriver *chr, int event);

void char_driver_add_handlers(CharDriver *s,
                              IOCanReadHandler *fd_can_read,
                              IOReadHandler *fd_read,
                              IOEventHandler *fd_event,
                              void *opaque);

#endif
