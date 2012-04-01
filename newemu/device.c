#include "newemu/device.h"

#include <stdio.h>
#include <stdarg.h>

//#define DEBUG_DEVICE

#ifdef DEBUG_DEVICE
#define device_log(dev, fmt, ...) \
    printf("DEVICE[%s]: " fmt "\n", dev->id, ## __VA_ARGS__)
#else
#define device_log(dev, fmt, ...) do { } while (0)
#endif

struct device_cleanup_data
{
    device_cleanup_func *cb;
    void *opaque;
};

void device_init(struct device *dev, struct clock *clock, const char *name, ...)
{
    va_list ap;

    dev->lock = g_mutex_new();

    va_start(ap, name);
    vsnprintf(dev->id, sizeof(dev->id), name, ap);
    va_end(ap);

    device_log(dev, "initialized");

    dev->clock = clock;
    dev->cleanup = NULL;
}

void device_add_cleanup_handler(struct device *dev, device_cleanup_func *cb,
                                void *data)
{
    struct device_cleanup_data *d = g_malloc0(sizeof(*d));

    d->cb = cb;
    d->opaque = data;

    dev->cleanup = g_slist_prepend(dev->cleanup, d);
}

static void device_cleanup_timer(struct device *dev, void *data)
{
    struct timer *t = data;
    timer_cleanup(t);
}

void device_init_timer(struct device *dev, struct timer *timer,
                       timer_callback *cb, const char *name, ...)
{
    va_list ap;
    char buffer[32];

    va_start(ap, name);
    snprintf(buffer, sizeof(buffer), "%s::%s", dev->id, name);
    timer_initv(timer, dev->clock, cb, buffer, ap);
    va_end(ap);

    device_add_cleanup_handler(dev, device_cleanup_timer, timer);
}

static void device_cleanup_pin(struct device *dev, void *data)
{
    struct pin *p = data;
    pin_cleanup(p);
}

void device_init_pin(struct device *dev, struct pin *pin,
                     const char *name, ...)
{
    va_list ap;
    char buffer[32];

    va_start(ap, name);
    snprintf(buffer, sizeof(buffer), "%s::%s", dev->id, name);
    pin_initv(pin, buffer, ap);
    va_end(ap);

    device_add_cleanup_handler(dev, device_cleanup_pin, pin);
}

void device_cleanup(struct device *dev)
{
    GSList *i;

    device_lock(dev);

    while ((i = dev->cleanup)) {
        struct device_cleanup_data *d = i->data;

        d->cb(dev, d->opaque);

        dev->cleanup = i->next;
        i->next = NULL;

        g_free(d);
        g_slist_free(i);
    }

    device_log(dev, "finalized");

    device_unlock(dev);
    g_mutex_free(dev->lock);
}

void device_lock(struct device *dev)
{
    g_mutex_lock(dev->lock);
}

void device_unlock(struct device *dev)
{
    g_mutex_unlock(dev->lock);
}
