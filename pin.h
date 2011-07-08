#ifndef PIN_H
#define PIN_H

#include "device.h"
#include "notify.h"

typedef struct Pin
{
    Device parent;

    /* private */
    bool level;

    /* FIXME */
    NotifierList level_changed;
} Pin;

#define TYPE_PIN "pin"
#define PIN(obj) TYPE_CHECK(Pin, obj, TYPE_PIN)

void pin_initialize(Pin *pin, const char *id);
void pin_finalize(Pin *pin);

void pin_set_level(Pin *pin, bool value);
bool pin_get_level(Pin *pin);

#endif
