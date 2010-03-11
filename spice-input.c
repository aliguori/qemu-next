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

/* mouse bits */

typedef struct QemuSpiceMouse {
    SpiceMouseInstance sin;
} QemuSpiceMouse;

static void mouse_motion(SpiceMouseInstance *sin, int dx, int dy, int dz,
                         uint32_t buttons_state)
{
    kbd_mouse_event(dx, dy, dz, buttons_state);
}

static void mouse_buttons(SpiceMouseInstance *sin, uint32_t buttons_state)
{
    kbd_mouse_event(0, 0, 0, buttons_state);
}

static const SpiceMouseInterface mouse_interface = {
    .base.type          = SPICE_INTERFACE_MOUSE,
    .base.description   = "mouse",
    .base.major_version = SPICE_INTERFACE_MOUSE_MAJOR,
    .base.minor_version = SPICE_INTERFACE_MOUSE_MINOR,
    .motion             = mouse_motion,
    .buttons            = mouse_buttons,
};

void qemu_spice_input_init(void)
{
    QemuSpiceKbd *kbd;
    QemuSpiceMouse *mouse;

    kbd = qemu_mallocz(sizeof(*kbd));
    kbd->sin.base.sif = &kbd_interface.base;
    spice_server_add_interface(spice_server, &kbd->sin.base);
    qemu_add_led_event_handler(kbd_leds, kbd);

    mouse = qemu_mallocz(sizeof(*mouse));
    mouse->sin.base.sif = &mouse_interface.base;
    spice_server_add_interface(spice_server, &mouse->sin.base);
}
