#ifndef TIMER_H
#define TIMER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include "newemu/clock.h"

#define container_of(obj, type, member) \
    (type *)((char *)(obj) - offsetof(type, member))

/* we use a PIMPL pattern here to avoid having to pull qemu dependencies into
   the header file. */

/* the caller must synchronize access to this object itself */

struct timer_impl;
struct timer;

typedef void (timer_callback)(struct timer *t);

struct timer
{
    char id[32];
    struct clock *clock;
    timer_callback *cb;
    struct timer_impl *impl;
};

void timer_init(struct timer *t, struct clock *c, timer_callback *cb,
                const char *name, ...)
    __attribute__((format(printf, 4, 5)));

void timer_initv(struct timer *t, struct clock *c, timer_callback *cb,
                 const char *name, va_list ap);

void timer_cleanup(struct timer *t);

bool timer_is_pending(struct timer *t);

void timer_cancel(struct timer *t);

uint64_t timer_get_deadline_ns(struct timer *t);

void timer_set_deadline_ns(struct timer *t, uint64_t value);

static inline uint64_t timer_get_deadline_us(struct timer *t)
{
    return timer_get_deadline_ns(t) / 1000L;
}

static inline uint64_t timer_get_deadline_ms(struct timer *t)
{
    return timer_get_deadline_us(t) / 1000L;
}

static inline uint64_t timer_get_deadline_sec(struct timer *t)
{
    return timer_get_deadline_ms(t) / 1000L;
}

static inline void timer_set_deadline_us(struct timer *t, uint64_t value)
{
    timer_set_deadline_ns(t, value * 1000L);
}

static inline void timer_set_deadline_ms(struct timer *t, uint64_t value)
{
    timer_set_deadline_us(t, value * 1000L);
}

static inline void timer_set_deadline_sec(struct timer *t, uint64_t value)
{
    timer_set_deadline_ms(t, value * 1000L);
}

static inline void timer_set_deadline_rel_ns(struct timer *t, uint64_t value)
{
    timer_set_deadline_ns(t, clock_get_ns(t->clock) + value);
}

static inline void timer_set_deadline_rel_us(struct timer *t, uint64_t value)
{
    timer_set_deadline_rel_ns(t, value * 1000L);
}

static inline void timer_set_deadline_rel_ms(struct timer *t, uint64_t value)
{
    timer_set_deadline_rel_us(t, value * 1000L);
}

static inline void timer_set_deadline_rel_sec(struct timer *t, uint64_t value)
{
    timer_set_deadline_rel_ms(t, value * 1000L);
}

#endif
