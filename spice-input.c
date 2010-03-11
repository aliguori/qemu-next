#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <spice.h>

#include "qemu-common.h"
#include "qemu-spice.h"
#include "console.h"

/* keyboard bits */

static int ledstate;

static void kbd_push_scan_freg(KeyboardInterface *keyboard, uint8_t frag);
static uint8_t kbd_get_leds(KeyboardInterface *keyboard);
static void kbd_leds(void *opaque, int l);

static KeyboardInterface kbd_interface = {
    .base.base_version = VM_INTERFACE_VERSION,
    .base.type = VD_INTERFACE_KEYBOARD,
    .base.description = "keyboard",
    .base.major_version = VD_INTERFACE_KEYBOARD_MAJOR,
    .base.minor_version = VD_INTERFACE_KEYBOARD_MINOR,
    .push_scan_freg = kbd_push_scan_freg,
    .get_leds = kbd_get_leds,
};

static void kbd_push_scan_freg(KeyboardInterface *keyboard, uint8_t frag)
{
    kbd_put_keycode(frag);
}

static uint8_t kbd_get_leds(KeyboardInterface *keyboard)
{
    return ledstate;
}

static void kbd_leds(void *opaque, int l)
{
    SpiceServer *s = opaque;
    ledstate = l;
    spice_server_kbd_leds(s, &kbd_interface, ledstate);
}

/* mouse bits */

static void mouse_motion(MouseInterface* mouse, int dx, int dy, int dz,
                         uint32_t buttons_state)
{
    kbd_mouse_event(dx, dy, dz, buttons_state);
}

static void mouse_buttons(MouseInterface* mouse, uint32_t buttons_state)
{
    kbd_mouse_event(0, 0, 0, buttons_state);
}

static MouseInterface mouse_interface = {
    .base.base_version = VM_INTERFACE_VERSION,
    .base.type = VD_INTERFACE_MOUSE,
    .base.description = "mouse",
    .base.major_version = VD_INTERFACE_MOUSE_MAJOR,
    .base.minor_version = VD_INTERFACE_MOUSE_MINOR,
    .moution = mouse_motion,
    .buttons = mouse_buttons,
};

void qemu_spice_input_init(SpiceServer *s)
{
    qemu_add_led_event_handler(kbd_leds, s);
    qemu_spice_add_interface(&kbd_interface.base);
    qemu_spice_add_interface(&mouse_interface.base);
}
