#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
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

typedef struct QemuSpicePointer {
    SpiceMouseInstance  mouse;
    SpiceTabletInstance tablet;
    int width, height, x, y;
    Notifier mouse_mode;
    bool absolute;
} QemuSpicePointer;

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

static void tablet_set_logical_size(SpiceTabletInstance* sin, int width, int height)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, tablet);

    fprintf(stderr, "%s: %dx%d\n", __FUNCTION__, width, height);
    if (height < 16)
        height = 16;
    if (width < 16)
        width = 16;
    pointer->width  = width;
    pointer->height = height;
}

static void tablet_position(SpiceTabletInstance* sin, int x, int y,
                            uint32_t buttons_state)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, tablet);

    pointer->x = x * 0x7FFF / (pointer->width - 1);
    pointer->y = y * 0x7FFF / (pointer->height - 1);
    kbd_mouse_event(pointer->x, pointer->y, 0, buttons_state);
}


static void tablet_wheel(SpiceTabletInstance* sin, int wheel,
                         uint32_t buttons_state)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, tablet);

    kbd_mouse_event(pointer->x, pointer->y, wheel, buttons_state);
}

static void tablet_buttons(SpiceTabletInstance *sin,
                           uint32_t buttons_state)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, tablet);

    kbd_mouse_event(pointer->x, pointer->y, 0, buttons_state);
}

static const SpiceTabletInterface tablet_interface = {
    .base.type          = SPICE_INTERFACE_TABLET,
    .base.description   = "tablet",
    .base.major_version = SPICE_INTERFACE_TABLET_MAJOR,
    .base.minor_version = SPICE_INTERFACE_TABLET_MINOR,
    .set_logical_size   = tablet_set_logical_size,
    .position           = tablet_position,
    .wheel              = tablet_wheel,
    .buttons            = tablet_buttons,
};

static void mouse_mode_notifier(Notifier *notifier)
{
    QemuSpicePointer *pointer = container_of(notifier, QemuSpicePointer, mouse_mode);
    bool is_absolute  = kbd_mouse_is_absolute();
    bool has_absolute = kbd_mouse_has_absolute();

    fprintf(stderr, "%s: absolute pointer: %s%s\n", __FUNCTION__,
            has_absolute ? "present" : "not available",
            is_absolute  ? "+active" : "");

    if (pointer->absolute == is_absolute)
        return;

    if (is_absolute) {
        fprintf(stderr, "%s: using absolute pointer (client mode)\n", __FUNCTION__);
        spice_server_add_interface(spice_server, &pointer->tablet.base);
    } else {
        fprintf(stderr, "%s: using relative pointer (server mode)\n", __FUNCTION__);
        spice_server_remove_interface(&pointer->tablet.base);
    }
    pointer->absolute = is_absolute;
}

void qemu_spice_input_init(void)
{
    QemuSpiceKbd *kbd;
    QemuSpicePointer *pointer;

    kbd = qemu_mallocz(sizeof(*kbd));
    kbd->sin.base.sif = &kbd_interface.base;
    spice_server_add_interface(spice_server, &kbd->sin.base);
    qemu_add_led_event_handler(kbd_leds, kbd);

    pointer = qemu_mallocz(sizeof(*pointer));
    pointer->mouse.base.sif  = &mouse_interface.base;
    pointer->tablet.base.sif = &tablet_interface.base;
    spice_server_add_interface(spice_server, &pointer->mouse.base);

    pointer->absolute = false;
    pointer->mouse_mode.notify = mouse_mode_notifier;
    qemu_add_mouse_mode_change_notifier(&pointer->mouse_mode);
    mouse_mode_notifier(&pointer->mouse_mode);
}
