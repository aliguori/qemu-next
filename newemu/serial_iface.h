#ifndef SERIAL_INTERFACE_H
#define SERIAL_INTERFACE_H

#include <errno.h>

#define SIF_TIOCM_CTS	0x020
#define SIF_TIOCM_CAR	0x040
#define SIF_TIOCM_DSR	0x100
#define SIF_TIOCM_RI	0x080
#define SIF_TIOCM_DTR	0x002
#define SIF_TIOCM_RTS	0x004

struct serial_interface;

struct serial_interface_ops
{
    void (*set_params)(struct serial_interface *sif,
                       int speed, int parity,
                       int data_bits, int stop_bits);

    void (*accept_input)(struct serial_interface *sif);

    void (*set_tiocm)(struct serial_interface *sif, int flags);

    int (*get_tiocm)(struct serial_interface *sif);

    void (*set_break)(struct serial_interface *sif, int break_enable);

    int (*send)(struct serial_interface *sif, uint8_t value);
};

struct serial_interface
{
    struct serial_interface_ops *ops;
};

static inline void sif_set_params(struct serial_interface *sif,
                                  int speed, int parity,
                                  int data_bits, int stop_bits)
{
    if (!sif->ops || !sif->ops->set_params) {
        return;
    }

    sif->ops->set_params(sif, speed, parity, data_bits, stop_bits);
}

static inline void sif_accept_input(struct serial_interface *sif)
{
    if (!sif->ops || !sif->ops->accept_input) {
        return;
    }

    sif->ops->accept_input(sif);
}

static inline void sif_set_tiocm(struct serial_interface *sif, int flags)
{
    if (!sif->ops || !sif->ops->set_tiocm) {
        return;
    }

    sif->ops->set_tiocm(sif, flags);
}

static inline int sif_get_tiocm(struct serial_interface *sif)
{
    if (!sif->ops || !sif->ops->get_tiocm) {
        return -ENOTSUP;
    }

    return sif->ops->get_tiocm(sif);
}

static inline void sif_set_break(struct serial_interface *sif, int break_enable)
{
    if (!sif->ops || !sif->ops->set_break) {
        return;
    }

    sif->ops->set_break(sif, break_enable);
}

static inline int sif_send(struct serial_interface *sif, uint8_t value)
{
    if (!sif->ops || !sif->ops->send) {
        return 0;
    }

    return sif->ops->send(sif, value);
}

#endif
