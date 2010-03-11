#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <spice.h>

#include "qemu-common.h"
#include "qemu-spice.h"
#include "console.h"

/* keyboard bits */

typedef struct QemuSpiceKbd {
    SpiceKbdInstance sin;
    int ledstate;
} QemuSpiceKbd;

static void kbd_push_key(SpiceKbdInstance *sin, uint8_t frag);
static uint8_t kbd_get_leds(SpiceKbdInstance *sin);
static void kbd_leds(void *opaque, int l);

static const SpiceKbdInterface kbd_interface = {
    .base.type          = SPICE_INTERFACE_KEYBOARD,
    .base.description   = "qemu keyboard",
    .base.major_version = SPICE_INTERFACE_KEYBOARD_MAJOR,
    .base.minor_version = SPICE_INTERFACE_KEYBOARD_MINOR,
    .push_scan_freg     = kbd_push_key,
    .get_leds           = kbd_get_leds,
};

static void kbd_push_key(SpiceKbdInstance *sin, uint8_t frag)
{
    kbd_put_keycode(frag);
}

static uint8_t kbd_get_leds(SpiceKbdInstance *sin)
{
    QemuSpiceKbd *kbd = container_of(sin, QemuSpiceKbd, sin);
    return kbd->ledstate;
}

static void kbd_leds(void *opaque, int ledstate)
{
    QemuSpiceKbd *kbd = opaque;
    kbd->ledstate = ledstate;
    spice_server_kbd_leds(&kbd->sin, ledstate);
}

void qemu_spice_input_init(void)
{
    QemuSpiceKbd *kbd;

    kbd = qemu_mallocz(sizeof(*kbd));
    kbd->sin.base.sif = &kbd_interface.base;
    spice_server_add_interface(spice_server, &kbd->sin.base);
    qemu_add_led_event_handler(kbd_leds, kbd);
}
