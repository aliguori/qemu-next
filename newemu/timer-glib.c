#include "newemu/timer.h"
#include "newemu/clock-glib.h"

struct timer_impl
{
    guint tag;
    uint64_t deadline;
};

static gboolean timer_trampoline(void *opaque)
{
    struct timer *t = opaque;

    t->cb(t);

    t->impl->tag = 0;

    return FALSE;
}

void timer_init(struct timer *t, struct clock *c, timer_callback *cb)
{
    g_assert(t && c && cb);

    t->clock = c;
    t->cb = cb;
    t->impl = g_malloc0(sizeof(struct timer_impl));

    t->impl->tag = 0;
    t->impl->deadline = -1ULL;
}

void timer_cleanup(struct timer *t)
{
    timer_cancel(t);
    g_free(t->impl);
    t->impl = NULL;
}

bool timer_is_pending(struct timer *t)
{
    return !!t->impl->tag;
}

void timer_cancel(struct timer *t)
{
    if (t->impl->tag) {
        g_source_remove(t->impl->tag);
        t->impl->tag = 0;
    }

    t->impl->deadline = -1ULL;
}

uint64_t timer_get_deadline_ns(struct timer *t)
{
    return t->impl->deadline;
}

void timer_set_deadline_ns(struct timer *t, uint64_t value)
{
    timer_cancel(t);

    t->impl->deadline = value;

    g_timeout_add((value - clock_get_ns(t->clock)) / 1000000UL,
                  timer_trampoline, t);
}
