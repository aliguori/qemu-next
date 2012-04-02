#include "newemu/pin.h"

#include <stdio.h>

//#define DEBUG_PIN

#ifdef DEBUG_PIN
#define pin_log(s, fmt, ...) \
    printf("PIN[%s]: " fmt "\n", (s)->id, ## __VA_ARGS__)
#else
#define pin_log(s, fmt, ...) do { if (0) \
    printf("PIN[%s]: " fmt "\n", (s)->id, ## __VA_ARGS__); \
} while (0)
#endif

void pin_initv(struct pin *p, const char *name, va_list ap)
{
    p->lock = g_mutex_new();
    vsnprintf(p->id, sizeof(p->id), name, ap);
    p->level = false;
}

void pin_init(struct pin *p, const char *name, ...)
{
    va_list ap;

    va_start(ap, name);
    pin_initv(p, name, ap);
    va_end(ap);

    pin_log(p, "initialized");
}

void pin_cleanup(struct pin *p)
{
    g_mutex_lock(p->lock);
    pin_log(p, "finalized");
    g_mutex_unlock(p->lock);

    g_mutex_free(p->lock);
}

void pin_set_level(struct pin *p, bool value)
{
    bool old_level;

    g_mutex_lock(p->lock);
    old_level = p->level;
    p->level = value;

    if (p->level != old_level) {
        pin_log(p, "level changed from %d -> %d", old_level, p->level);
    }

    g_mutex_unlock(p->lock);
}

bool pin_get_level(struct pin *p)
{
    bool value;

    g_mutex_lock(p->lock);
    value = p->level;
    g_mutex_unlock(p->lock);

    return value;
}

