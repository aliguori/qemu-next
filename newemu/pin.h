#ifndef PIN_H
#define PIN_H

#include <stdbool.h>
#include <glib.h>

struct pin
{
    GMutex *lock;
    bool level;
};

void pin_init(struct pin *p);

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
