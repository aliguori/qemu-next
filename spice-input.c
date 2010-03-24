#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <spice.h>

#include "qemu-common.h"
#include "qemu-spice.h"
#include "console.h"

/* keyboard bits */

static int ledstate;
static bool have_tablet;
static SpiceServer *spice_state;
static int abs_x, abs_y, abs_width, abs_height;

static void qemu_spice_tablet_mode(bool enabled);
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
    if (kbd_mouse_is_absolute()) {
        /*
         * We'll arrive here when the guest activates some input
         * device with absolute positioning, i.e. usb tablet.
         */
        qemu_spice_tablet_mode(true);
        return;
    }
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

/* tablet bits */

static void tablet_set_logical_size(TabletInterface* interface, int width, int height)
{
    abs_width  = width;
    abs_height = height;
}

static void tablet_position(TabletInterface *interface, int x, int y,
                            uint32_t buttons_state)
{
    if (!kbd_mouse_is_absolute()) {
        qemu_spice_tablet_mode(false);
        return;
    }
    abs_x = x * 0x7FFF / (abs_width - 1);
    abs_y = y * 0x7FFF / (abs_height - 1);
    kbd_mouse_event(abs_x, abs_y, 0, buttons_state);
}


static void tablet_wheel(TabletInterface *interface, int wheel,
                         uint32_t buttons_state)
{
    kbd_mouse_event(abs_x, abs_y, wheel, buttons_state);
}

static void tablet_buttons(TabletInterface *interface,
                           uint32_t buttons_state)
{
    kbd_mouse_event(abs_x, abs_y, 0, buttons_state);
}

static TabletInterface tablet_interface = {
    .base.base_version = VM_INTERFACE_VERSION,
    .base.type = VD_INTERFACE_TABLET,
    .base.description = "tablet",
    .base.major_version = VD_INTERFACE_TABLET_MAJOR,
    .base.minor_version = VD_INTERFACE_TABLET_MINOR,
    .set_logical_size = tablet_set_logical_size,
    .position = tablet_position,
    .wheel = tablet_wheel,
    .buttons = tablet_buttons,
};

void qemu_spice_input_init(SpiceServer *s)
{
    spice_state = s;
    qemu_add_led_event_handler(kbd_leds, s);
    qemu_spice_add_interface(&kbd_interface.base);
    qemu_spice_add_interface(&mouse_interface.base);
}

static void qemu_spice_tablet_mode(bool enabled)
{
    if (enabled) {
        if (!have_tablet) {
            qemu_spice_add_interface(&tablet_interface.base);
            have_tablet = true;
        }
        spice_server_set_mouse_absolute(spice_state, 1);
    } else {
        if (have_tablet) {
            qemu_spice_remove_interface(&tablet_interface.base);
            have_tablet = false;
        }
        spice_server_set_mouse_absolute(spice_state, 0);
    }
}

void qemu_spice_tablet_size(int width, int height)
{
    tablet_set_logical_size(&tablet_interface, width, height);
}
