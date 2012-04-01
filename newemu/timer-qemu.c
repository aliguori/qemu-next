#include "newemu/timer.h"
#include "newemu/clock-qemu.h"

struct timer_impl
{
    QEMUTimer *qemu_timer;
};

static void timer_trampoline(void *opaque)
{
    struct timer *t = opaque;

    t->cb(t);
}

void timer_init(struct timer *t, struct clock *c, timer_callback *cb)
{
    g_assert(t && c && cb);

    t->clock = c;
    t->cb = cb;
    t->impl = g_malloc0(sizeof(struct timer_impl));

    t->impl->qemu_timer = qemu_new_timer_ns(t->clock->impl->clock,
                                            timer_trampoline, t);
}

void timer_cleanup(struct timer *t)
{
    qemu_free_timer(t->impl->qemu_timer);
    g_free(t->impl);
    t->impl = NULL;
}

bool timer_is_pending(struct timer *t)
{
    return !!qemu_timer_pending(t->impl->qemu_timer);
}

void timer_cancel(struct timer *t)
{
    qemu_del_timer(t->impl->qemu_timer);
}

uint64_t timer_get_deadline_ns(struct timer *t)
{
    return qemu_timer_expire_time_ns(t->impl->qemu_timer);
}

void timer_set_deadline_ns(struct timer *t, uint64_t value)
{
    qemu_mod_timer_ns(t->impl->qemu_timer, value);
}
