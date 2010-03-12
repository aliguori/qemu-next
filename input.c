/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#include "sysemu.h"
#include "net.h"
#include "monitor.h"
#include "console.h"
#include "qjson.h"

static QTAILQ_HEAD(, KeyboardEventHandler) keyboard_handlers =
    QTAILQ_HEAD_INITIALIZER(keyboard_handlers);

static QTAILQ_HEAD(, QEMUPutMouseEntry) mouse_handlers =
    QTAILQ_HEAD_INITIALIZER(mouse_handlers);

static QTAILQ_HEAD(, QEMUPutLEDEntry) led_handlers =
    QTAILQ_HEAD_INITIALIZER(led_handlers);

static NotifierList mouse_mode_notifiers = 
    NOTIFIER_LIST_INITIALIZER(mouse_mode_notifiers);

void keyboard_add_handler(KeyboardEventHandler *handler)
{
    QTAILQ_INSERT_TAIL(&keyboard_handlers, handler, node);
}

void keyboard_remove_handler(KeyboardEventHandler *handler)
{
    QTAILQ_REMOVE(&keyboard_handlers, handler, node);
}

static void mouse_check_mode_change(void)
{
    static int current_is_absolute, current_has_absolute;
    int is_absolute;
    int has_absolute;

    is_absolute = mouse_current_is_absolute();
    has_absolute = mouse_has_absolute();

    if (is_absolute != current_is_absolute ||
        has_absolute != current_has_absolute) {
        notifier_list_notify(&mouse_mode_notifiers);
    }

    current_is_absolute = is_absolute;
    current_has_absolute = has_absolute;
}

void mouse_add_handler(MouseEventHandler *handler)
{
    static int next_mouse_id;

    handler->id = next_mouse_id++
    QTAILQ_INSERT_TAIL(&mouse_handlers, handler, node);
    mouse_check_mode_change();
}

void mouse_remove_handler(MouseEventHandler *handler)
{
    QTAILQ_REMOVE(&mouse_handlers, handler, node);
    mouse_check_mode_change();
}

void mouse_activate_handler(MouseEventHandler *handler)
{
    mouse_remove_handler(handler);
    QTAILQ_INSERT_HEAD(&mouse_handlers, handler, node);
    mouse_check_mode_change();
}

void led_add_handler(LEDEventHandler *handler)
{
    QTAILQ_INSERT_TAIL(&led_handlers, handler, node);
}

void led_remove_handler(LEDEventHandler *handler)
{
    QTAILQ_REMOVE(&led_handlers, handler, node);
}

void qemu_activate_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    QTAILQ_REMOVE(&mouse_handlers, entry, node);
    QTAILQ_INSERT_HEAD(&mouse_handlers, entry, node);

    check_mode_change();
}

void qemu_remove_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    QTAILQ_REMOVE(&mouse_handlers, entry, node);

    qemu_free(entry->qemu_put_mouse_event_name);
    qemu_free(entry);

    check_mode_change();
}

void keyboard_put_keycode(int keycode)
{
    KeyboardEventHandler *handler;

    QTAILQ_FOREACH(handler, &keyboard_handlers, node) {
        handler->callback(handler, keycode);
    }
}

void led_set_state(int ledstate)
{
    LEDEventHandler *handler;

    QTAILQ_FOREACH(handler, &led_handlers, node) {
        handler->callback(handler, ledstate);
    }
}

void mouse_put_event(int dx, int dy, int dz, int buttons_state)
{
    MouseEventHandler *handler;
    int width;

    if (QTAILQ_EMPTY(&mouse_handlers)) {
        return;
    }

    handler = QTAILQ_FIRST(&mouse_handlers);

    /* FIXME this doesn't belong here */
    if (graphic_rotate) {
        int tmp;
        if (handler->absolute)
            width = 0x7fff;
        else
            width = graphic_width - 1;

        tmp = width - dy;
        dy = dx;
        dx = tmp;
    }

    handler->callback(handler, dx, dy, dz, buttons_state);
}

int mouse_current_is_absolute(void)
{
    if (QTAILQ_EMPTY(&mouse_handlers)) {
        return 0;
    }

    return QTAILQ_FIRST(&mouse_handlers)->absolute;
}

int mouse_has_absolute(void)
{
    QEMUPutMouseEntry *entry;

    QTAILQ_FOREACH(entry, &mouse_handlers, node) {
        if (entry->absolute) {
            return 1;
        }
    }

    return 0;
}

static void info_mice_iter(QObject *data, void *opaque)
{
    QDict *mouse;
    Monitor *mon = opaque;

    mouse = qobject_to_qdict(data);
    monitor_printf(mon, "%c Mouse #%" PRId64 ": %s%s\n",
                  (qdict_get_bool(mouse, "current") ? '*' : ' '),
                   qdict_get_int(mouse, "index"), qdict_get_str(mouse, "name"),
                   qdict_get_bool(mouse, "absolute") ? " (absolute)" : "");
}

void do_info_mice_print(Monitor *mon, const QObject *data)
{
    QList *mice_list;

    mice_list = qobject_to_qlist(data);
    if (qlist_empty(mice_list)) {
        monitor_printf(mon, "No mouse devices connected\n");
        return;
    }

    qlist_iter(mice_list, info_mice_iter, mon);
}

/**
 * do_info_mice(): Show VM mice information
 *
 * Each mouse is represented by a QDict, the returned QObject is a QList of
 * all mice.
 *
 * The mouse QDict contains the following:
 *
 * - "name": mouse's name
 * - "index": mouse's index
 * - "current": true if this mouse is receiving events, false otherwise
 * - "absolute": true if the mouse generates absolute input events
 *
 * Example:
 *
 * [ { "name": "QEMU Microsoft Mouse", "index": 0, "current": false, "absolute": false },
 *   { "name": "QEMU PS/2 Mouse", "index": 1, "current": true, "absolute": true } ]
 */
void do_info_mice(Monitor *mon, QObject **ret_data)
{
    MouseEventHandler *cursor;
    QList *mice_list;
    int current;

    mice_list = qlist_new();

    if (QTAILQ_EMPTY(&mouse_handlers)) {
        goto out;
    }

    current = QTAILQ_FIRST(&mouse_handlers)->id;

    QTAILQ_FOREACH(cursor, &mouse_handlers, node) {
        QObject *obj;
        obj = qobject_from_jsonf("{ 'name': %s,"
                                 "  'index': %d,"
                                 "  'current': %i,"
                                 "  'absolute': %i }",
                                 cursor->name,
                                 cursor->id,
                                 cursor->id == current,
                                 !!cursor->absolute);
        qlist_append_obj(mice_list, obj);
    }

out:
    *ret_data = QOBJECT(mice_list);
}

void do_mouse_set(Monitor *mon, const QDict *qdict)
{
    MouseEventHandler *cursor;
    int index = qdict_get_int(qdict, "index");
    int found = 0;

    if (QTAILQ_EMPTY(&mouse_handlers)) {
        monitor_printf(mon, "No mouse devices connected\n");
        return;
    }

    QTAILQ_FOREACH(cursor, &mouse_handlers, node) {
        if (cursor->id == index) {
            found = 1;
            mouse_activate_handler(cursor);
            break;
        }
    }

    if (!found) {
        monitor_printf(mon, "Mouse at given index not found\n");
    }

    mouse_check_mode_change();
}

void mouse_add_change_notifier(Notifier *notify)
{
    notifier_list_add(&mouse_mode_notifiers, notify);
}

void mouse_remove_change_notifier(Notifier *notify)
{
    notifier_list_remove(&mouse_mode_notifiers, notify);
}
