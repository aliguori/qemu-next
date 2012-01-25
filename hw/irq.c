/*
 * QEMU IRQ/GPIO common code.
 *
 * Copyright (c) 2007 CodeSourcery.
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
#include "qemu-common.h"
#include "irq.h"
#include "qdev.h"

void qemu_set_irq(qemu_irq irq, int level)
{
    if (!irq)
        return;

    irq->handler(irq->opaque, irq->n, level);
}

qemu_irq *qemu_allocate_irqs(qemu_irq_handler handler, void *opaque, int n)
{
    qemu_irq *s;
    struct IRQState *p;
    int i;

    s = (qemu_irq *)g_malloc0(sizeof(qemu_irq) * n);
    p = (struct IRQState *)g_malloc0(sizeof(struct IRQState) * n);
    for (i = 0; i < n; i++) {
        p->handler = handler;
        p->opaque = opaque;
        p->n = i;
        s[i] = p;
        p++;
    }
    return s;
}

void qemu_free_irqs(qemu_irq *s)
{
    g_free(s[0]);
    g_free(s);
}

static void qemu_notirq(void *opaque, int line, int level)
{
    struct IRQState *irq = opaque;

    irq->handler(irq->opaque, irq->n, !level);
}

qemu_irq qemu_irq_invert(qemu_irq irq)
{
    /* The default state for IRQs is low, so raise the output now.  */
    qemu_irq_raise(irq);
    return qemu_allocate_irqs(qemu_notirq, irq, 1)[0];
}

static void qemu_splitirq(void *opaque, int line, int level)
{
    struct IRQState **irq = opaque;
    irq[0]->handler(irq[0]->opaque, irq[0]->n, level);
    irq[1]->handler(irq[1]->opaque, irq[1]->n, level);
}

qemu_irq qemu_irq_split(qemu_irq irq1, qemu_irq irq2)
{
    qemu_irq *s = g_malloc0(2 * sizeof(qemu_irq));
    s[0] = irq1;
    s[1] = irq2;
    return qemu_allocate_irqs(qemu_splitirq, s, 1)[0];
}

static void proxy_irq_handler(void *opaque, int n, int level)
{
    qemu_irq **target = opaque;

    if (*target) {
        qemu_set_irq((*target)[n], level);
    }
}

qemu_irq *qemu_irq_proxy(qemu_irq **target, int n)
{
    return qemu_allocate_irqs(proxy_irq_handler, target, n);
}

void pin_set_level(Pin *pin, bool level)
{
    bool old_level = pin->level;

    pin->level = level;

    if (old_level != pin->level) {
        notifier_list_notify(&pin->level_change_notifiers, pin);
    }
}

bool pin_get_level(Pin *pin)
{
    return pin->level;
}

void pin_add_level_change_notifier(Pin *pin, Notifier *notifier)
{
    notifier_list_add(&pin->level_change_notifiers, notifier);
}

void pin_del_level_change_notifier(Pin *pin, Notifier *notifier)
{
    notifier_list_remove(&pin->level_change_notifiers, notifier);
}

static void pin_init(Object *obj)
{
    Pin *pin = PIN(obj);

    notifier_list_init(&pin->level_change_notifiers);
}

static TypeInfo pin_info = {
    .name = TYPE_PIN,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(Pin),
    .instance_init = pin_init,
};

#define TYPE_WIRE "wire"
#define WIRE(obj) OBJECT_CHECK(Wire, (obj), TYPE_WIRE)

typedef struct Wire
{
    DeviceState parent;

    Pin *pin[2];
    Notifier notifier[2];
} Wire;

static void wire_init(Object *obj)
{
    Wire *wire = WIRE(obj);

    object_property_add_link(obj, "pin[0]", TYPE_PIN,
                             (Object **)&wire->pin[0], NULL);
    object_property_add_link(obj, "pin[1]", TYPE_PIN,
                             (Object **)&wire->pin[1], NULL);
}

static void wire_update_pin1(Notifier *notifier, void *data)
{
    Wire *wire = container_of(notifier, Wire, notifier[0]);

    pin_set_level(wire->pin[1], pin_get_level(wire->pin[0]));
}

static void wire_update_pin0(Notifier *notifier, void *data)
{
    Wire *wire = container_of(notifier, Wire, notifier[1]);

    pin_set_level(wire->pin[0], pin_get_level(wire->pin[1]));
}

static int wire_realize(DeviceState *dev)
{
    Wire *wire = WIRE(dev);

    if (!wire->pin[0] || !wire->pin[1]) {
        return -1;
    }

    wire->notifier[0].notify = wire_update_pin1;
    pin_add_level_change_notifier(wire->pin[0], &wire->notifier[0]);

    wire->notifier[1].notify = wire_update_pin0;
    pin_add_level_change_notifier(wire->pin[1], &wire->notifier[1]);

    return 0;
}

static void wire_cleanup(Object *obj)
{
    Wire *wire = WIRE(obj);

    pin_del_level_change_notifier(wire->pin[0], &wire->notifier[0]);
    pin_del_level_change_notifier(wire->pin[1], &wire->notifier[1]);
}

static void wire_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->init = wire_realize;
}

static TypeInfo wire_info = {
    .name = TYPE_WIRE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Wire),
    .instance_init = wire_init,
    .instance_finalize = wire_cleanup,
    .class_init = wire_class_init,
};

/** Utilities to wire Pins and qemu_irqs **/

typedef struct PinIrqConnection
{
    Notifier notifier;
    Pin *pin;
    qemu_irq irq;
} PinIrqConnection;

static void pin_irq_update_status(Notifier *notifier, void *data)
{
    PinIrqConnection *c = container_of(notifier, PinIrqConnection, notifier);

    qemu_set_irq(c->irq, pin_get_level(c->pin));
}

void pin_connect_qemu_irq(Pin *in, qemu_irq out)
{
    PinIrqConnection *c = g_malloc0(sizeof(*c));

    c->notifier.notify = pin_irq_update_status;
    c->irq = out;
    c->pin = in;

    pin_add_level_change_notifier(c->pin, &c->notifier);
}

void pin_connect_pin(Pin *in, Pin *out)
{
    Wire *wire = WIRE(object_new(TYPE_WIRE));

    wire->pin[0] = in;
    wire->pin[1] = out;

    qdev_init_nofail(DEVICE(wire));
}

static void register_devices(void)
{
    type_register_static(&pin_info);
    type_register_static(&wire_info);
}

device_init(register_devices);
