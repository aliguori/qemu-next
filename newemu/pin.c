#include "pin.h"

void pin_init(struct pin *p)
{
    p->level = false;
}

void pin_cleanup(struct pin *p)
{
}

void pin_set_level(struct pin *p, bool value)
{
    p->level = value;
}

bool pin_get_level(struct pin *p)
{
    return p->level;
}

