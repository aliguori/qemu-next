#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <spice.h>
#include <spice-experimental.h>

#include "qemu-common.h"
#include "qemu-spice.h"
#include "qemu-timer.h"
#include "qemu-queue.h"
#include "monitor.h"

/* core bits */

SpiceServer *spice_server;
int using_spice = 0;

struct SpiceTimer {
    QEMUTimer *timer;
    QTAILQ_ENTRY(SpiceTimer) next;
};
static QTAILQ_HEAD(, SpiceTimer) timers = QTAILQ_HEAD_INITIALIZER(timers);

static SpiceTimer *timer_add(SpiceTimerFunc func, void *opaque)
{
    SpiceTimer *timer;

    timer = qemu_mallocz(sizeof(*timer));
    timer->timer = qemu_new_timer(rt_clock, func, opaque);
    QTAILQ_INSERT_TAIL(&timers, timer, next);
    return timer;
}

static void timer_start(SpiceTimer *timer, uint32_t ms)
{
    qemu_mod_timer(timer->timer, qemu_get_clock(rt_clock) + ms);
}

static void timer_cancel(SpiceTimer *timer)
{
    qemu_del_timer(timer->timer);
}

static void timer_remove(SpiceTimer *timer)
{
    qemu_del_timer(timer->timer);
    qemu_free_timer(timer->timer);
    QTAILQ_REMOVE(&timers, timer, next);
    free(timer);
}

struct SpiceWatch {
    int fd;
    int event_mask;
    SpiceWatchFunc func;
    void *opaque;
    QTAILQ_ENTRY(SpiceWatch) next;
};
static QTAILQ_HEAD(, SpiceWatch) watches = QTAILQ_HEAD_INITIALIZER(watches);

static void watch_read(void *opaque)
{
    SpiceWatch *watch = opaque;
    watch->func(watch->fd, SPICE_WATCH_EVENT_READ, watch->opaque);
}

static void watch_write(void *opaque)
{
    SpiceWatch *watch = opaque;
    watch->func(watch->fd, SPICE_WATCH_EVENT_WRITE, watch->opaque);
}

static void watch_update_mask(SpiceWatch *watch, int event_mask)
{
    IOHandler *on_read = NULL;
    IOHandler *on_write = NULL;

    watch->event_mask = event_mask;
    if (watch->event_mask & SPICE_WATCH_EVENT_READ)
        on_read = watch_read;
    if (watch->event_mask & SPICE_WATCH_EVENT_WRITE)
        on_read = watch_write;
    qemu_set_fd_handler(watch->fd, on_read, on_write, watch);
}

static SpiceWatch *watch_add(int fd, int event_mask, SpiceWatchFunc func, void *opaque)
{
    SpiceWatch *watch;

    watch = qemu_mallocz(sizeof(*watch));
    watch->fd     = fd;
    watch->func   = func;
    watch->opaque = opaque;
    QTAILQ_INSERT_TAIL(&watches, watch, next);
    fprintf(stderr, "%s: watch %p, fd %d\n", __FUNCTION__, watch, fd);

    watch_update_mask(watch, event_mask);
    return watch;
}

static void watch_remove(SpiceWatch *watch)
{
    fprintf(stderr, "%s: watch %p\n", __FUNCTION__, watch);
    watch_update_mask(watch, 0);
    QTAILQ_REMOVE(&watches, watch, next);
    qemu_free(watch);
}

static SpiceCoreInterface core_interface = {
    .base.type          = SPICE_INTERFACE_CORE,
    .base.description   = "qemu core services",
    .base.major_version = SPICE_INTERFACE_CORE_MAJOR,
    .base.minor_version = SPICE_INTERFACE_CORE_MINOR,

    .timer_add          = timer_add,
    .timer_start        = timer_start,
    .timer_cancel       = timer_cancel,
    .timer_remove       = timer_remove,

    .watch_add          = watch_add,
    .watch_update_mask  = watch_update_mask,
    .watch_remove       = watch_remove,
};

/* functions for the rest of qemu */

void qemu_spice_init(void)
{
    QemuOpts *opts = QTAILQ_FIRST(&qemu_spice_opts.head);
    const char *password;
    int port;

    if (!opts)
        return;
    port = qemu_opt_get_number(opts, "port", 0);
    if (!port)
        return;
    password = qemu_opt_get(opts, "password");

    spice_server = spice_server_new();
    spice_server_set_port(spice_server, port);
    if (password)
        spice_server_set_ticket(spice_server, password, 0, 0, 0);
    if (qemu_opt_get_bool(opts, "disable-ticketing", 0))
        spice_server_set_noauth(spice_server);

    /* TODO: make configurable via cmdline */
    spice_server_set_image_compression(spice_server, SPICE_IMAGE_COMPRESS_AUTO_GLZ);

    spice_server_init(spice_server, &core_interface);
    using_spice = 1;
}
