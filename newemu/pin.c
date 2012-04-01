#include "pin.h"

void pin_init(struct pin *p)
{
    p->lock = g_mutex_new();
    p->level = false;
}

void pin_cleanup(struct pin *p)
{
    g_mutex_free(p->lock);
}

void pin_set_level(struct pin *p, bool value)
{
    g_mutex_lock(p->lock);
    p->level = value;
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

