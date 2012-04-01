#ifndef PIN_H
#define PIN_H

#include <stdbool.h>
#include <stdarg.h>
#include <glib.h>

struct pin
{
    char id[32];
    GMutex *lock;
    bool level;
};

void pin_init(struct pin *p, const char *name, ...)
    __attribute__((format(printf, 2, 3)));

void pin_initv(struct pin *p, const char *name, va_list ap);

void pin_cleanup(struct pin *p);

void pin_set_level(struct pin *p, bool value);

bool pin_get_level(struct pin *p);

static inline void pin_raise(struct pin *p)
{
    pin_set_level(p, true);
}

static inline void pin_lower(struct pin *p)
{
    pin_set_level(p, false);
}

static inline void pin_pulse(struct pin *p)
{
    pin_raise(p);
    pin_lower(p);
}

#endif
