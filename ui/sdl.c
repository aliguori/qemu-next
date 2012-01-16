/*
 * QEMU SDL display driver
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* Avoid compiler warning because macro is redefined in SDL_syswm.h. */
#undef WIN32_LEAN_AND_MEAN

#include <SDL.h>
#include <SDL_syswm.h>

#include "qemu-common.h"
#include "console.h"
#include "sysemu.h"
#include "x_keymap.h"
#include "sdl_zoom.h"
#include "keymaps.h"

typedef struct SDLDisplayState SDLDisplayState;

struct SDLDisplayState
{
    DisplayState *ds;

    DisplayChangeListener *dcl;
    SDL_Surface *real_screen;
    SDL_Surface *guest_screen;
    int gui_grab; /* if true, all keyboard/mouse events are grabbed */
    int last_vm_running;
    bool gui_saved_scaling;
    int gui_saved_width;
    int gui_saved_height;
    int gui_saved_grab;
    int gui_fullscreen;
    int gui_noframe;
    int gui_key_modifier_pressed;
    int gui_keysym;
    int gui_grab_code;
    uint8_t modifiers_state[256];
    SDL_Cursor *sdl_cursor_normal;
    SDL_Cursor *sdl_cursor_hidden;
    int absolute_enabled;
    int guest_cursor;
    int guest_x, guest_y;
    SDL_Cursor *guest_sprite;
    uint8_t allocator;
    SDL_PixelFormat host_format;
    int scaling_active;
    Notifier mouse_mode_notifier;
    Notifier exit_notifier;
    kbd_layout_t *kbd_layout;
};

static SDLDisplayState *global_sdl_state;

static SDLDisplayState *to_sdl_display(DisplayState *ds)
{
    return ds->opaque;
}

static DisplayState *to_display(SDLDisplayState *s)
{
    return s->ds;
}

static void sdl_update(DisplayState *ds, int x, int y, int w, int h)
{
    SDLDisplayState *s = to_sdl_display(ds);
    SDL_Rect rec;

    rec.x = x;
    rec.y = y;
    rec.w = w;
    rec.h = h;

    if (s->guest_screen) {
        if (!s->scaling_active) {
            SDL_BlitSurface(s->guest_screen, &rec, s->real_screen, &rec);
        } else {
            if (sdl_zoom_blit(s->guest_screen, s->real_screen, SMOOTHING_ON, &rec) < 0) {
                fprintf(stderr, "Zoom blit failed\n");
                exit(1);
            }
        }
    } 
    SDL_UpdateRect(s->real_screen, rec.x, rec.y, rec.w, rec.h);
}

static void sdl_setdata(DisplayState *ds)
{
    SDLDisplayState *s = to_sdl_display(ds);

    if (s->guest_screen != NULL) SDL_FreeSurface(s->guest_screen);

    s->guest_screen = SDL_CreateRGBSurfaceFrom(ds_get_data(ds), ds_get_width(ds), ds_get_height(ds),
                                               ds_get_bits_per_pixel(ds), ds_get_linesize(ds),
                                               ds->surface->pf.rmask, ds->surface->pf.gmask,
                                               ds->surface->pf.bmask, ds->surface->pf.amask);
}

static void do_sdl_resize(SDLDisplayState *s, int width, int height, int bpp)
{
    int flags;

    flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL;
    if (s->gui_fullscreen) {
        flags |= SDL_FULLSCREEN;
    } else {
        flags |= SDL_RESIZABLE;
    }
    if (s->gui_noframe)
        flags |= SDL_NOFRAME;

    s->real_screen = SDL_SetVideoMode(width, height, bpp, flags);
    if (!s->real_screen) {
	fprintf(stderr, "Could not open SDL display (%dx%dx%d): %s\n", width, 
		height, bpp, SDL_GetError());
        exit(1);
    }
}

static void sdl_resize(DisplayState *ds)
{
    SDLDisplayState *s = to_sdl_display(ds);

    if (!s->allocator) {
        if (!s->scaling_active)
            do_sdl_resize(s, ds_get_width(ds), ds_get_height(ds), 0);
        else if (s->real_screen->format->BitsPerPixel != ds_get_bits_per_pixel(ds))
            do_sdl_resize(s, s->real_screen->w, s->real_screen->h, ds_get_bits_per_pixel(ds));
        sdl_setdata(ds);
    } else {
        if (s->guest_screen != NULL) {
            SDL_FreeSurface(s->guest_screen);
            s->guest_screen = NULL;
        }
    }
}

static PixelFormat sdl_to_qemu_pixelformat(SDL_PixelFormat *sdl_pf)
{
    PixelFormat qemu_pf;

    memset(&qemu_pf, 0x00, sizeof(PixelFormat));

    qemu_pf.bits_per_pixel = sdl_pf->BitsPerPixel;
    qemu_pf.bytes_per_pixel = sdl_pf->BytesPerPixel;
    qemu_pf.depth = (qemu_pf.bits_per_pixel) == 32 ? 24 : (qemu_pf.bits_per_pixel);

    qemu_pf.rmask = sdl_pf->Rmask;
    qemu_pf.gmask = sdl_pf->Gmask;
    qemu_pf.bmask = sdl_pf->Bmask;
    qemu_pf.amask = sdl_pf->Amask;

    qemu_pf.rshift = sdl_pf->Rshift;
    qemu_pf.gshift = sdl_pf->Gshift;
    qemu_pf.bshift = sdl_pf->Bshift;
    qemu_pf.ashift = sdl_pf->Ashift;

    qemu_pf.rbits = 8 - sdl_pf->Rloss;
    qemu_pf.gbits = 8 - sdl_pf->Gloss;
    qemu_pf.bbits = 8 - sdl_pf->Bloss;
    qemu_pf.abits = 8 - sdl_pf->Aloss;

    qemu_pf.rmax = ((1 << qemu_pf.rbits) - 1);
    qemu_pf.gmax = ((1 << qemu_pf.gbits) - 1);
    qemu_pf.bmax = ((1 << qemu_pf.bbits) - 1);
    qemu_pf.amax = ((1 << qemu_pf.abits) - 1);

    return qemu_pf;
}

static DisplaySurface* sdl_create_displaysurface(int width, int height)
{
    SDLDisplayState *s = global_sdl_state;
    DisplaySurface *surface = (DisplaySurface*) g_malloc0(sizeof(DisplaySurface));

    if (surface == NULL) {
        fprintf(stderr, "sdl_create_displaysurface: malloc failed\n");
        exit(1);
    }

    surface->width = width;
    surface->height = height;

    if (s->scaling_active) {
        int linesize;
        PixelFormat pf;
        if (s->host_format.BytesPerPixel != 2 && s->host_format.BytesPerPixel != 4) {
            linesize = width * 4;
            pf = qemu_default_pixelformat(32);
        } else {
            linesize = width * s->host_format.BytesPerPixel;
            pf = sdl_to_qemu_pixelformat(&s->host_format);
        }
        qemu_alloc_display(surface, width, height, linesize, pf, 0);
        return surface;
    }

    if (s->host_format.BitsPerPixel == 16)
        do_sdl_resize(s, width, height, 16);
    else
        do_sdl_resize(s, width, height, 32);

    surface->pf = sdl_to_qemu_pixelformat(s->real_screen->format);
    surface->linesize = s->real_screen->pitch;
    surface->data = s->real_screen->pixels;

#ifdef HOST_WORDS_BIGENDIAN
    surface->flags = QEMU_REALPIXELS_FLAG | QEMU_BIG_ENDIAN_FLAG;
#else
    surface->flags = QEMU_REALPIXELS_FLAG;
#endif
    s->allocator = 1;

    return surface;
}

static void sdl_free_displaysurface(DisplaySurface *surface)
{
    SDLDisplayState *s = global_sdl_state;

    s->allocator = 0;
    if (surface == NULL)
        return;

    if (surface->flags & QEMU_ALLOCATED_FLAG)
        g_free(surface->data);
    g_free(surface);
}

static DisplaySurface* sdl_resize_displaysurface(DisplaySurface *surface, int width, int height)
{
    sdl_free_displaysurface(surface);
    return sdl_create_displaysurface(width, height);
}

/* generic keyboard conversion */

#include "sdl_keysym.h"

static uint8_t sdl_keyevent_to_keycode_generic(SDLDisplayState *s, const SDL_KeyboardEvent *ev)
{
    int keysym;
    /* workaround for X11+SDL bug with AltGR */
    keysym = ev->keysym.sym;
    if (keysym == 0 && ev->keysym.scancode == 113)
        keysym = SDLK_MODE;
    /* For Japanese key '\' and '|' */
    if (keysym == 92 && ev->keysym.scancode == 133) {
        keysym = 0xa5;
    }
    return keysym2scancode(s->kbd_layout, keysym) & SCANCODE_KEYMASK;
}

/* specific keyboard conversions from scan codes */

#if defined(_WIN32)

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    return ev->keysym.scancode;
}

#else

#if defined(SDL_VIDEO_DRIVER_X11)
#include <X11/XKBlib.h>

static int check_for_evdev(void)
{
    SDL_SysWMinfo info;
    XkbDescPtr desc = NULL;
    int has_evdev = 0;
    char *keycodes = NULL;

    SDL_VERSION(&info.version);
    if (!SDL_GetWMInfo(&info)) {
        return 0;
    }
    desc = XkbGetKeyboard(info.info.x11.display,
                          XkbGBN_AllComponentsMask,
                          XkbUseCoreKbd);
    if (desc && desc->names) {
        keycodes = XGetAtomName(info.info.x11.display, desc->names->keycodes);
        if (keycodes == NULL) {
            fprintf(stderr, "could not lookup keycode name\n");
        } else if (strstart(keycodes, "evdev", NULL)) {
            has_evdev = 1;
        } else if (!strstart(keycodes, "xfree86", NULL)) {
            fprintf(stderr, "unknown keycodes `%s', please report to "
                    "qemu-devel@nongnu.org\n", keycodes);
        }
    }

    if (desc) {
        XkbFreeKeyboard(desc, XkbGBN_AllComponentsMask, True);
    }
    if (keycodes) {
        XFree(keycodes);
    }
    return has_evdev;
}
#else
static int check_for_evdev(void)
{
	return 0;
}
#endif

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    int keycode;
    static int has_evdev = -1;

    if (has_evdev == -1)
        has_evdev = check_for_evdev();

    keycode = ev->keysym.scancode;

    if (keycode < 9) {
        keycode = 0;
    } else if (keycode < 97) {
        keycode -= 8; /* just an offset */
    } else if (keycode < 158) {
        /* use conversion table */
        if (has_evdev)
            keycode = translate_evdev_keycode(keycode - 97);
        else
            keycode = translate_xfree86_keycode(keycode - 97);
    } else if (keycode == 208) { /* Hiragana_Katakana */
        keycode = 0x70;
    } else if (keycode == 211) { /* backslash */
        keycode = 0x73;
    } else {
        keycode = 0;
    }
    return keycode;
}

#endif

static void reset_keys(SDLDisplayState *s)
{
    int i;
    for(i = 0; i < 256; i++) {
        if (s->modifiers_state[i]) {
            if (i & SCANCODE_GREY)
                kbd_put_keycode(SCANCODE_EMUL0);
            kbd_put_keycode(i | SCANCODE_UP);
            s->modifiers_state[i] = 0;
        }
    }
}

static void sdl_process_key(SDLDisplayState *s, SDL_KeyboardEvent *ev)
{
    int keycode, v;

    if (ev->keysym.sym == SDLK_PAUSE) {
        /* specific case */
        v = 0;
        if (ev->type == SDL_KEYUP)
            v |= SCANCODE_UP;
        kbd_put_keycode(0xe1);
        kbd_put_keycode(0x1d | v);
        kbd_put_keycode(0x45 | v);
        return;
    }

    if (s->kbd_layout) {
        keycode = sdl_keyevent_to_keycode_generic(s, ev);
    } else {
        keycode = sdl_keyevent_to_keycode(ev);
    }

    switch(keycode) {
    case 0x00:
        /* sent when leaving window: reset the modifiers state */
        reset_keys(s);
        return;
    case 0x2a:                          /* Left Shift */
    case 0x36:                          /* Right Shift */
    case 0x1d:                          /* Left CTRL */
    case 0x9d:                          /* Right CTRL */
    case 0x38:                          /* Left ALT */
    case 0xb8:                         /* Right ALT */
        if (ev->type == SDL_KEYUP)
            s->modifiers_state[keycode] = 0;
        else
            s->modifiers_state[keycode] = 1;
        break;
#define QEMU_SDL_VERSION ((SDL_MAJOR_VERSION << 8) + SDL_MINOR_VERSION)
#if QEMU_SDL_VERSION < 0x102 || QEMU_SDL_VERSION == 0x102 && SDL_PATCHLEVEL < 14
        /* SDL versions before 1.2.14 don't support key up for caps/num lock. */
    case 0x45: /* num lock */
    case 0x3a: /* caps lock */
        /* SDL does not send the key up event, so we generate it */
        kbd_put_keycode(keycode);
        kbd_put_keycode(keycode | SCANCODE_UP);
        return;
#endif
    }

    /* now send the key code */
    if (keycode & SCANCODE_GREY)
        kbd_put_keycode(SCANCODE_EMUL0);
    if (ev->type == SDL_KEYUP)
        kbd_put_keycode(keycode | SCANCODE_UP);
    else
        kbd_put_keycode(keycode & SCANCODE_KEYCODEMASK);
}

static void sdl_update_caption(SDLDisplayState *s)
{
    char win_title[1024];
    char icon_title[1024];
    const char *status = "";

    if (!runstate_is_running())
        status = " [Stopped]";
    else if (s->gui_grab) {
        if (alt_grab)
            status = " - Press Ctrl-Alt-Shift to exit mouse grab";
        else if (ctrl_grab)
            status = " - Press Right-Ctrl to exit mouse grab";
        else
            status = " - Press Ctrl-Alt to exit mouse grab";
    }

    if (qemu_name) {
        snprintf(win_title, sizeof(win_title), "QEMU (%s)%s", qemu_name, status);
        snprintf(icon_title, sizeof(icon_title), "QEMU (%s)", qemu_name);
    } else {
        snprintf(win_title, sizeof(win_title), "QEMU%s", status);
        snprintf(icon_title, sizeof(icon_title), "QEMU");
    }

    SDL_WM_SetCaption(win_title, icon_title);
}

static void sdl_hide_cursor(SDLDisplayState *s)
{
    if (!cursor_hide)
        return;

    if (kbd_mouse_is_absolute()) {
        SDL_ShowCursor(1);
        SDL_SetCursor(s->sdl_cursor_hidden);
    } else {
        SDL_ShowCursor(0);
    }
}

static void sdl_show_cursor(SDLDisplayState *s)
{
    if (!cursor_hide)
        return;

    if (!kbd_mouse_is_absolute() || !is_graphic_console()) {
        SDL_ShowCursor(1);
        if (s->guest_cursor &&
                (s->gui_grab || kbd_mouse_is_absolute() || s->absolute_enabled))
            SDL_SetCursor(s->guest_sprite);
        else
            SDL_SetCursor(s->sdl_cursor_normal);
    }
}

static void sdl_grab_start(SDLDisplayState *s)
{
    if (s->guest_cursor) {
        SDL_SetCursor(s->guest_sprite);
        if (!kbd_mouse_is_absolute() && !s->absolute_enabled)
            SDL_WarpMouse(s->guest_x, s->guest_y);
    } else
        sdl_hide_cursor(s);

    if (SDL_WM_GrabInput(SDL_GRAB_ON) == SDL_GRAB_ON) {
        s->gui_grab = 1;
        sdl_update_caption(s);
    } else
        sdl_show_cursor(s);
}

static void sdl_grab_end(SDLDisplayState *s)
{
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    s->gui_grab = 0;
    sdl_show_cursor(s);
    sdl_update_caption(s);
}

static void sdl_mouse_mode_change(Notifier *notify, void *data)
{
    SDLDisplayState *s = container_of(notify, SDLDisplayState, mouse_mode_notifier);

    if (kbd_mouse_is_absolute()) {
        if (!s->absolute_enabled) {
            sdl_grab_start(s);
            s->absolute_enabled = 1;
        }
    } else if (s->absolute_enabled) {
        if (!s->gui_fullscreen) {
            sdl_grab_end(s);
        }
        s->absolute_enabled = 0;
    }
}

static void sdl_send_mouse_event(SDLDisplayState *s, int dx, int dy, int dz, int x, int y, int state)
{
    int buttons = 0;

    if (state & SDL_BUTTON(SDL_BUTTON_LEFT)) {
        buttons |= MOUSE_EVENT_LBUTTON;
    }
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
        buttons |= MOUSE_EVENT_RBUTTON;
    }
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) {
        buttons |= MOUSE_EVENT_MBUTTON;
    }

    if (kbd_mouse_is_absolute()) {
        dx = x * 0x7FFF / (s->real_screen->w - 1);
        dy = y * 0x7FFF / (s->real_screen->h - 1);
    } else if (s->guest_cursor) {
        x -= s->guest_x;
        y -= s->guest_y;
        s->guest_x += x;
        s->guest_y += y;
        dx = x;
        dy = y;
    }

    kbd_mouse_event(dx, dy, dz, buttons);
}

static void sdl_scale(SDLDisplayState *s, int width, int height)
{
    DisplayState *ds = to_display(s);
    int bpp = s->real_screen->format->BitsPerPixel;

    if (bpp != 16 && bpp != 32) {
        bpp = 32;
    }
    do_sdl_resize(s, width, height, bpp);
    s->scaling_active = 1;
    if (!is_buffer_shared(ds->surface)) {
        ds->surface = qemu_resize_displaysurface(ds, ds_get_width(ds),
                                                 ds_get_height(ds));
        dpy_resize(ds);
    }
}

static void toggle_full_screen(SDLDisplayState *s)
{
    DisplayState *ds = to_display(s);

    s->gui_fullscreen = !s->gui_fullscreen;
    if (s->gui_fullscreen) {
        s->gui_saved_width = s->real_screen->w;
        s->gui_saved_height = s->real_screen->h;
        s->gui_saved_scaling = s->scaling_active;

        do_sdl_resize(s, ds_get_width(ds), ds_get_height(ds),
                      ds_get_bits_per_pixel(ds));
        s->scaling_active = 0;

        s->gui_saved_grab = s->gui_grab;
        sdl_grab_start(s);
    } else {
        if (s->gui_saved_scaling) {
            sdl_scale(s, s->gui_saved_width, s->gui_saved_height);
        } else {
            do_sdl_resize(s, ds_get_width(ds), ds_get_height(ds), 0);
        }
        if (!s->gui_saved_grab || !is_graphic_console()) {
            sdl_grab_end(s);
        }
    }
    vga_hw_invalidate();
    vga_hw_update();
}

static void absolute_mouse_grab(SDLDisplayState *s)
{
    int mouse_x, mouse_y;

    if (SDL_GetAppState() & SDL_APPINPUTFOCUS) {
        SDL_GetMouseState(&mouse_x, &mouse_y);
        if (mouse_x > 0 && mouse_x < s->real_screen->w - 1 &&
            mouse_y > 0 && mouse_y < s->real_screen->h - 1) {
            sdl_grab_start(s);
        }
    }
}

static void handle_keydown(DisplayState *ds, SDL_Event *ev)
{
    SDLDisplayState *s = to_sdl_display(ds);
    int mod_state;
    int keycode;

    if (alt_grab) {
        mod_state = (SDL_GetModState() & (s->gui_grab_code | KMOD_LSHIFT)) ==
                    (s->gui_grab_code | KMOD_LSHIFT);
    } else if (ctrl_grab) {
        mod_state = (SDL_GetModState() & KMOD_RCTRL) == KMOD_RCTRL;
    } else {
        mod_state = (SDL_GetModState() & s->gui_grab_code) == s->gui_grab_code;
    }
    s->gui_key_modifier_pressed = mod_state;

    if (s->gui_key_modifier_pressed) {
        keycode = sdl_keyevent_to_keycode(&ev->key);
        switch (keycode) {
        case 0x21: /* 'f' key on US keyboard */
            toggle_full_screen(s);
            s->gui_keysym = 1;
            break;
        case 0x16: /* 'u' key on US keyboard */
            if (s->scaling_active) {
                s->scaling_active = 0;
                sdl_resize(ds);
                vga_hw_invalidate();
                vga_hw_update();
            }
            s->gui_keysym = 1;
            break;
        case 0x02 ... 0x0a: /* '1' to '9' keys */
            /* Reset the modifiers sent to the current console */
            reset_keys(s);
            console_select(keycode - 0x02);
            s->gui_keysym = 1;
            if (s->gui_fullscreen) {
                break;
            }
            if (!is_graphic_console()) {
                /* release grab if going to a text console */
                if (s->gui_grab) {
                    sdl_grab_end(s);
                } else if (s->absolute_enabled) {
                    sdl_show_cursor(s);
                }
            } else if (s->absolute_enabled) {
                sdl_hide_cursor(s);
                absolute_mouse_grab(s);
            }
            break;
        case 0x1b: /* '+' */
        case 0x35: /* '-' */
            if (!s->gui_fullscreen) {
                int width = MAX(s->real_screen->w + (keycode == 0x1b ? 50 : -50),
                                160);
                int height = (ds_get_height(ds) * width) / ds_get_width(ds);

                sdl_scale(s, width, height);
                vga_hw_invalidate();
                vga_hw_update();
                s->gui_keysym = 1;
            }
        default:
            break;
        }
    } else if (!is_graphic_console()) {
        int keysym = 0;

        if (ev->key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)) {
            switch (ev->key.keysym.sym) {
            case SDLK_UP:
                keysym = QEMU_KEY_CTRL_UP;
                break;
            case SDLK_DOWN:
                keysym = QEMU_KEY_CTRL_DOWN;
                break;
            case SDLK_LEFT:
                keysym = QEMU_KEY_CTRL_LEFT;
                break;
            case SDLK_RIGHT:
                keysym = QEMU_KEY_CTRL_RIGHT;
                break;
            case SDLK_HOME:
                keysym = QEMU_KEY_CTRL_HOME;
                break;
            case SDLK_END:
                keysym = QEMU_KEY_CTRL_END;
                break;
            case SDLK_PAGEUP:
                keysym = QEMU_KEY_CTRL_PAGEUP;
                break;
            case SDLK_PAGEDOWN:
                keysym = QEMU_KEY_CTRL_PAGEDOWN;
                break;
            default:
                break;
            }
        } else {
            switch (ev->key.keysym.sym) {
            case SDLK_UP:
                keysym = QEMU_KEY_UP;
                break;
            case SDLK_DOWN:
                keysym = QEMU_KEY_DOWN;
                break;
            case SDLK_LEFT:
                keysym = QEMU_KEY_LEFT;
                break;
            case SDLK_RIGHT:
                keysym = QEMU_KEY_RIGHT;
                break;
            case SDLK_HOME:
                keysym = QEMU_KEY_HOME;
                break;
            case SDLK_END:
                keysym = QEMU_KEY_END;
                break;
            case SDLK_PAGEUP:
                keysym = QEMU_KEY_PAGEUP;
                break;
            case SDLK_PAGEDOWN:
                keysym = QEMU_KEY_PAGEDOWN;
                break;
            case SDLK_BACKSPACE:
                keysym = QEMU_KEY_BACKSPACE;
                break;
            case SDLK_DELETE:
                keysym = QEMU_KEY_DELETE;
                break;
            default:
                break;
            }
        }
        if (keysym) {
            kbd_put_keysym(keysym);
        } else if (ev->key.keysym.unicode != 0) {
            kbd_put_keysym(ev->key.keysym.unicode);
        }
    }
    if (is_graphic_console() && !s->gui_keysym) {
        sdl_process_key(s, &ev->key);
    }
}

static void handle_keyup(SDLDisplayState *s, SDL_Event *ev)
{
    int mod_state;

    if (!alt_grab) {
        mod_state = (ev->key.keysym.mod & s->gui_grab_code);
    } else {
        mod_state = (ev->key.keysym.mod & (s->gui_grab_code | KMOD_LSHIFT));
    }
    if (!mod_state && s->gui_key_modifier_pressed) {
        s->gui_key_modifier_pressed = 0;
        if (s->gui_keysym == 0) {
            /* exit/enter grab if pressing Ctrl-Alt */
            if (!s->gui_grab) {
                /* If the application is not active, do not try to enter grab
                 * state. It prevents 'SDL_WM_GrabInput(SDL_GRAB_ON)' from
                 * blocking all the application (SDL bug). */
                if (is_graphic_console() &&
                    SDL_GetAppState() & SDL_APPACTIVE) {
                    sdl_grab_start(s);
                }
            } else if (!s->gui_fullscreen) {
                sdl_grab_end(s);
            }
            /* SDL does not send back all the modifiers key, so we must
             * correct it. */
            reset_keys(s);
            return;
        }
        s->gui_keysym = 0;
    }
    if (is_graphic_console() && !s->gui_keysym) {
        sdl_process_key(s, &ev->key);
    }
}

static void handle_mousemotion(SDLDisplayState *s, SDL_Event *ev)
{
    int max_x, max_y;

    if (is_graphic_console() &&
        (kbd_mouse_is_absolute() || s->absolute_enabled)) {
        max_x = s->real_screen->w - 1;
        max_y = s->real_screen->h - 1;
        if (s->gui_grab && (ev->motion.x == 0 || ev->motion.y == 0 ||
            ev->motion.x == max_x || ev->motion.y == max_y)) {
            sdl_grab_end(s);
        }
        if (!s->gui_grab && SDL_GetAppState() & SDL_APPINPUTFOCUS &&
            (ev->motion.x > 0 && ev->motion.x < max_x &&
            ev->motion.y > 0 && ev->motion.y < max_y)) {
            sdl_grab_start(s);
        }
    }
    if (s->gui_grab || kbd_mouse_is_absolute() || s->absolute_enabled) {
        sdl_send_mouse_event(s, ev->motion.xrel, ev->motion.yrel, 0,
                             ev->motion.x, ev->motion.y, ev->motion.state);
    }
}

static void handle_mousebutton(SDLDisplayState *s, SDL_Event *ev)
{
    int buttonstate = SDL_GetMouseState(NULL, NULL);
    SDL_MouseButtonEvent *bev;
    int dz;

    if (!is_graphic_console()) {
        return;
    }

    bev = &ev->button;
    if (!s->gui_grab && !kbd_mouse_is_absolute()) {
        if (ev->type == SDL_MOUSEBUTTONDOWN &&
            (bev->button == SDL_BUTTON_LEFT)) {
            /* start grabbing all events */
            sdl_grab_start(s);
        }
    } else {
        dz = 0;
        if (ev->type == SDL_MOUSEBUTTONDOWN) {
            buttonstate |= SDL_BUTTON(bev->button);
        } else {
            buttonstate &= ~SDL_BUTTON(bev->button);
        }
#ifdef SDL_BUTTON_WHEELUP
        if (bev->button == SDL_BUTTON_WHEELUP &&
            ev->type == SDL_MOUSEBUTTONDOWN) {
            dz = -1;
        } else if (bev->button == SDL_BUTTON_WHEELDOWN &&
                   ev->type == SDL_MOUSEBUTTONDOWN) {
            dz = 1;
        }
#endif
        sdl_send_mouse_event(s, 0, 0, dz, bev->x, bev->y, buttonstate);
    }
}

static void handle_activation(SDLDisplayState *s, SDL_Event *ev)
{
    if (s->gui_grab && ev->active.state == SDL_APPINPUTFOCUS &&
        !ev->active.gain && !s->gui_fullscreen) {
        sdl_grab_end(s);
    }
    if (!s->gui_grab && ev->active.gain && is_graphic_console() &&
        (kbd_mouse_is_absolute() || s->absolute_enabled)) {
        absolute_mouse_grab(s);
    }
    if (ev->active.state & SDL_APPACTIVE) {
        if (ev->active.gain) {
            /* Back to default interval */
            s->dcl->gui_timer_interval = 0;
            s->dcl->idle = 0;
        } else {
            /* Sleeping interval */
            s->dcl->gui_timer_interval = 500;
            s->dcl->idle = 1;
        }
    }
}

static void sdl_refresh(DisplayState *ds)
{
    SDLDisplayState *s = to_sdl_display(ds);
    SDL_Event ev1, *ev = &ev1;

    if (s->last_vm_running != runstate_is_running()) {
        s->last_vm_running = runstate_is_running();
        sdl_update_caption(s);
    }

    vga_hw_update();
    SDL_EnableUNICODE(!is_graphic_console());

    while (SDL_PollEvent(ev)) {
        switch (ev->type) {
        case SDL_VIDEOEXPOSE:
            sdl_update(ds, 0, 0, s->real_screen->w, s->real_screen->h);
            break;
        case SDL_KEYDOWN:
            handle_keydown(ds, ev);
            break;
        case SDL_KEYUP:
            handle_keyup(s, ev);
            break;
        case SDL_QUIT:
            if (!no_quit) {
                no_shutdown = 0;
                qemu_system_shutdown_request();
            }
            break;
        case SDL_MOUSEMOTION:
            handle_mousemotion(s, ev);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            handle_mousebutton(s, ev);
            break;
        case SDL_ACTIVEEVENT:
            handle_activation(s, ev);
            break;
        case SDL_VIDEORESIZE:
            sdl_scale(s, ev->resize.w, ev->resize.h);
            vga_hw_invalidate();
            vga_hw_update();
            break;
        default:
            break;
        }
    }
}

static void sdl_fill(DisplayState *ds, int x, int y, int w, int h, uint32_t c)
{
    SDLDisplayState *s = to_sdl_display(ds);
    SDL_Rect dst = { x, y, w, h };

    SDL_FillRect(s->real_screen, &dst, c);
}

static void sdl_mouse_warp(DisplayState *ds, int x, int y, int on)
{
    SDLDisplayState *s = to_sdl_display(ds);

    if (on) {
        if (!s->guest_cursor)
            sdl_show_cursor(s);
        if (s->gui_grab || kbd_mouse_is_absolute() || s->absolute_enabled) {
            SDL_SetCursor(s->guest_sprite);
            if (!kbd_mouse_is_absolute() && !s->absolute_enabled)
                SDL_WarpMouse(x, y);
        }
    } else if (s->gui_grab)
        sdl_hide_cursor(s);

    s->guest_cursor = on;
    s->guest_x = x;
    s->guest_y = y;
}

static void sdl_mouse_define(DisplayState *ds, QEMUCursor *c)
{
    SDLDisplayState *s = to_sdl_display(ds);
    uint8_t *image, *mask;
    int bpl;

    if (s->guest_sprite)
        SDL_FreeCursor(s->guest_sprite);

    bpl = cursor_get_mono_bpl(c);
    image = g_malloc0(bpl * c->height);
    mask  = g_malloc0(bpl * c->height);
    cursor_get_mono_image(c, 0x000000, image);
    cursor_get_mono_mask(c, 0, mask);
    s->guest_sprite = SDL_CreateCursor(image, mask, c->width, c->height,
                                       c->hot_x, c->hot_y);
    g_free(image);
    g_free(mask);

    if (s->guest_cursor &&
            (s->gui_grab || kbd_mouse_is_absolute() || s->absolute_enabled))
        SDL_SetCursor(s->guest_sprite);
}

static void sdl_cleanup(Notifier *notifier, void *data)
{
    SDLDisplayState *s = container_of(notifier, SDLDisplayState, exit_notifier);
    if (s->guest_sprite)
        SDL_FreeCursor(s->guest_sprite);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void sdl_display_init(DisplayState *ds, int full_screen, int no_frame)
{
    SDLDisplayState *s = g_malloc0(sizeof(*s));
    int flags;
    uint8_t data = 0;
    DisplayAllocator *da;
    const SDL_VideoInfo *vi;
    char *filename;

    ds->opaque = s;
    s->ds = ds;

    global_sdl_state = s;
    s->gui_grab_code = KMOD_LALT | KMOD_LCTRL;

#if defined(__APPLE__)
    /* always use generic keymaps */
    if (!keyboard_layout)
        keyboard_layout = "en-us";
#endif
    if (keyboard_layout) {
        s->kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout);
        if (!s->kbd_layout)
            exit(1);
    }

    if (no_frame)
        s->gui_noframe = 1;

    if (!full_screen) {
        setenv("SDL_VIDEO_ALLOW_SCREENSAVER", "1", 0);
    }
#ifdef __linux__
    /* on Linux, SDL may use fbcon|directfb|svgalib when run without
     * accessible $DISPLAY to open X11 window.  This is often the case
     * when qemu is run using sudo.  But in this case, and when actually
     * run in X11 environment, SDL fights with X11 for the video card,
     * making current display unavailable, often until reboot.
     * So make x11 the default SDL video driver if this variable is unset.
     * This is a bit hackish but saves us from bigger problem.
     * Maybe it's a good idea to fix this in SDL instead.
     */
    setenv("SDL_VIDEODRIVER", "x11", 0);
#endif

    /* Enable normal up/down events for Caps-Lock and Num-Lock keys.
     * This requires SDL >= 1.2.14. */
    setenv("SDL_DISABLE_LOCK_KEYS", "1", 1);

    flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE;
    if (SDL_Init (flags)) {
        fprintf(stderr, "Could not initialize SDL(%s) - exiting\n",
                SDL_GetError());
        exit(1);
    }
    vi = SDL_GetVideoInfo();
    s->host_format = *(vi->vfmt);

    /* Load a 32x32x4 image. White pixels are transparent. */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "qemu-icon.bmp");
    if (filename) {
        SDL_Surface *image = SDL_LoadBMP(filename);
        if (image) {
            uint32_t colorkey = SDL_MapRGB(image->format, 255, 255, 255);
            SDL_SetColorKey(image, SDL_SRCCOLORKEY, colorkey);
            SDL_WM_SetIcon(image, NULL);
        }
        g_free(filename);
    }

    if (full_screen) {
        s->gui_fullscreen = 1;
        sdl_grab_start(s);
    }

    s->dcl = g_malloc0(sizeof(DisplayChangeListener));
    s->dcl->dpy_update = sdl_update;
    s->dcl->dpy_resize = sdl_resize;
    s->dcl->dpy_refresh = sdl_refresh;
    s->dcl->dpy_setdata = sdl_setdata;
    s->dcl->dpy_fill = sdl_fill;
    ds->mouse_set = sdl_mouse_warp;
    ds->cursor_define = sdl_mouse_define;
    register_displaychangelistener(ds, s->dcl);

    da = g_malloc0(sizeof(DisplayAllocator));
    da->create_displaysurface = sdl_create_displaysurface;
    da->resize_displaysurface = sdl_resize_displaysurface;
    da->free_displaysurface = sdl_free_displaysurface;
    if (register_displayallocator(ds, da) == da) {
        dpy_resize(ds);
    }

    s->mouse_mode_notifier.notify = sdl_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&s->mouse_mode_notifier);

    sdl_update_caption(s);
    SDL_EnableKeyRepeat(250, 50);
    s->gui_grab = 0;

    s->sdl_cursor_hidden = SDL_CreateCursor(&data, &data, 8, 1, 0, 0);
    s->sdl_cursor_normal = SDL_GetCursor();

    s->exit_notifier.notify = sdl_cleanup;
    qemu_add_exit_notifier(&s->exit_notifier);
}
