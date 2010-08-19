#ifndef QEMU_SERIAL_INTERFACE_H
#define QEMU_SERIAL_INTERFACE_H

#include "qemu-common.h"
#include "notify.h"
#include "interface.h"

typedef struct SerialInterface SerialInterface;

typedef struct SerialInterfaceOperations
{
    ssize_t (*read)(SerialInterface *sif, void *buf, size_t size);
    ssize_t (*write)(SerialInterface *sif, const void *buf, size_t size);
    void (*set_params)(SerialInterface *sif,
                       int speed,
                       int parity,
                       int data_bits,
                       int stop_bits);
    int (*get_tiocm)(SerialInterface *sif);
    void (*set_tiocm)(SerialInterface *sif, int flags);
    void (*set_break)(SerialInterface *sif, int enable);
    void (*set_read_notify)(SerialInterface *sif, Notifier *notifier);
    void (*set_write_notify)(SerialInterface *sif, Notifier *notifier);
    void (*set_break_notify)(SerialInterface *sif, Notifier *notifier);
} SerialInterfaceOperations;

struct SerialInterface
{
    Interface iface;
    SerialInterfaceOperations *ops;
};

static inline ssize_t sif_read(SerialInterface *sif, void *buf, size_t size)
{
    return sif->ops->read(sif, buf, size);
}

static inline ssize_t sif_write(SerialInterface *sif,
                                const void *buf, size_t size)
{
    return sif->ops->write(sif, buf, size);
}

static inline void sif_set_params(SerialInterface *sif,
                                  int speed,
                                  int parity,
                                  int data_bits,
                                  int stop_bits)
{
    sif->ops->set_params(sif, speed, parity, data_bits, stop_bits);
}

static inline int sif_get_tiocm(SerialInterface *sif)
{
    return sif->ops->get_tiocm(sif);
}

static inline void sif_set_tiocm(SerialInterface *sif, int flags)
{
    sif->ops->set_tiocm(sif, flags);
}

static inline void sif_set_break(SerialInterface *sif, int enable)
{
    sif->ops->set_break(sif, enable);
}

static inline void sif_set_read_notify(SerialInterface *sif,
                                       Notifier *notifier)
{
    sif->ops->set_read_notify(sif, notifier);
}

static inline void sif_set_write_notify(SerialInterface *sif,
                                        Notifier *notifier)
{
    sif->ops->set_write_notify(sif, notifier);
}

static inline void sif_set_break_notify(SerialInterface *sif,
                                        Notifier *notifier)
{
    sif->ops->set_break_notify(sif, notifier);
}

#endif
