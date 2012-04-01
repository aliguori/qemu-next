#ifndef DEVICE_H
#define DEVICE_H

#include "newemu/clock.h"
#include "newemu/timer.h"
#include "newemu/pin.h"

#include <glib.h>

struct device {
    char id[32];
    GSList *cleanup;
    struct clock *clock;
    GMutex *lock;
};

typedef void (device_cleanup_func)(struct device *dev, void *opaque);

void device_init(struct device *dev, struct clock *clock, const char *name, ...)
    __attribute__((format(printf, 3, 4)));

void device_add_cleanup_handler(struct device *dev, device_cleanup_func *cb,
                                void *data);

void device_init_timer(struct device *dev, struct timer *timer,
                       timer_callback *cb, const char *name, ...)
    __attribute__((format(printf, 4, 5)));

void device_init_pin(struct device *dev, struct pin *pin,
                     const char *name, ...)
    __attribute__((format(printf, 3, 4)));

void device_cleanup(struct device *dev);

void device_lock(struct device *dev);

void device_unlock(struct device *dev);

#endif
