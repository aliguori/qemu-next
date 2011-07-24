#ifndef PIN_H
#define PIN_H

#include "qemu/device.h"
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
void pin_visit(Pin *pin, Visitor *v, const char *name, Error **errp);

void pin_set_level(Pin *pin, bool value, Error **errp);
bool pin_get_level(Pin *pin, Error **errp);

#endif
