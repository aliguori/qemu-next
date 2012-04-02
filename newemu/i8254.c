/*
 * QEMU 8253/8254 - common bits of emulated and KVM kernel model
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2012      Jan Kiszka, Siemens AG
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

#include "newemu/i8254.h"
#include "newemu/timer.h"
#include "newemu/pin.h"

#include <stdio.h>
#include <inttypes.h>

#define RW_STATE_LSB 1
#define RW_STATE_MSB 2
#define RW_STATE_WORD0 3
#define RW_STATE_WORD1 4

#define I8254_FREQ 1193182

static inline uint64_t muldiv64(uint64_t a, uint64_t b, uint64_t c)
{
    return (a * b) / c;
}

static int _i8254_get_count(struct i8254_channel *sc)
{
    uint64_t d;
    int counter;

    d = muldiv64(clock_get_ns(sc->dev.clock) - sc->count_load_time, I8254_FREQ,
                 NS_PER_SEC);
    switch (sc->mode) {
    case 0:
    case 1:
    case 4:
    case 5:
        counter = (sc->count - d) & 0xffff;
        break;
    case 3:
        /* XXX: may be incorrect for odd counts */
        counter = sc->count - ((2 * d) % sc->count);
        break;
    default:
        counter = sc->count - (d % sc->count);
        break;
    }

    return counter;
}

/* return -1 if no transition will occur.  */
static int64_t _i8254_get_next_transition_time(struct i8254_channel *sc,
                                               int64_t current_time)
{
    uint64_t d, next_time, base;
    int period2;

    d = muldiv64(current_time - sc->count_load_time, I8254_FREQ, NS_PER_SEC);
    switch (sc->mode) {
    default:
    case 0:
    case 1:
        if (d < sc->count) {
            next_time = sc->count;
        } else {
            return -1;
        }
        break;
    case 2:
        base = (d / sc->count) * sc->count;
        if ((d - base) == 0 && d != 0) {
            next_time = base + sc->count;
        } else {
            next_time = base + sc->count + 1;
        }
        break;
    case 3:
        base = (d / sc->count) * sc->count;
        period2 = ((sc->count + 1) >> 1);
        if ((d - base) < period2) {
            next_time = base + period2;
        } else {
            next_time = base + sc->count;
        }
        break;
    case 4:
    case 5:
        if (d < sc->count) {
            next_time = sc->count;
        } else if (d == sc->count) {
            next_time = sc->count + 1;
        } else {
            return -1;
        }
        break;
    }
    /* convert to timer units */
    next_time = sc->count_load_time + muldiv64(next_time, NS_PER_SEC,
                                              I8254_FREQ);
    /* fix potential rounding problems */
    /* XXX: better solution: use a clock at I8254_FREQ Hz */
    if (next_time <= current_time) {
        next_time = current_time + 1;
    }
    return next_time;
}

/* get i8254 output bit */
static int _i8254_get_out(struct i8254_channel *sc, int64_t current_time)
{
    uint64_t d;
    int out;

    d = muldiv64(current_time - sc->count_load_time, I8254_FREQ, NS_PER_SEC);
    switch (sc->mode) {
    default:
    case 0:
        out = (d >= sc->count);
        break;
    case 1:
        out = (d < sc->count);
        break;
    case 2:
        if ((d % sc->count) == 0 && d != 0) {
            out = 1;
        } else {
            out = 0;
        }
        break;
    case 3:
        out = (d % sc->count) < ((sc->count + 1) >> 1);
        break;
    case 4:
    case 5:
        out = (d == sc->count);
        break;
    }
    return out;
}

static void _i8254_irq_timer_update(struct i8254_channel *sc,
                                    int64_t current_time)
{
    int64_t expire_time;
    int irq_level;

    if (sc->irq_disabled) {
        return;
    }

    expire_time = _i8254_get_next_transition_time(sc, current_time);
    irq_level = _i8254_get_out(sc, current_time);
    pin_set_level(&sc->irq, irq_level);
    sc->next_transition_time = expire_time;

    if (expire_time != -1) {
        timer_set_deadline_ns(&sc->irq_timer, expire_time);
    } else {
        timer_cancel(&sc->irq_timer);
    }
}

/* val must be 0 or 1 */
void i8254_set_channel_gate(struct i8254 *s, int channel, int val)
{
    struct i8254_channel *sc;

    sc = &s->channels[channel];

    switch (sc->mode) {
    default:
    case 0:
    case 4:
        /* XXX: just disable/enable counting */
        break;
    case 1:
    case 5:
        if (sc->gate < val) {
            /* restart counting on rising edge */
            sc->count_load_time = clock_get_ns(s->dev.clock);
            _i8254_irq_timer_update(sc, sc->count_load_time);
        }
        break;
    case 2:
    case 3:
        if (sc->gate < val) {
            /* restart counting on rising edge */
            sc->count_load_time = clock_get_ns(s->dev.clock);
            _i8254_irq_timer_update(sc, sc->count_load_time);
        }
        /* XXX: disable/enable counting */
        break;
    }
    sc->gate = val;
}

static void _i8254_load_count(struct i8254_channel *sc, int val)
{
    if (val == 0) {
        val = 0x10000;
    }
    sc->count_load_time = clock_get_ns(sc->dev.clock);
    sc->count = val;
    _i8254_irq_timer_update(sc, sc->count_load_time);
}

/* if already latched, do not latch again */
static void _i8254_latch_count(struct i8254_channel *sc)
{
    if (!sc->count_latched) {
        sc->latched_count = _i8254_get_count(sc);
        sc->count_latched = sc->rw_mode;
    }
}

static void i8254_channel_io_write_rb(struct i8254_channel *sc, uint32_t val,
                                      int channel)
{
    device_lock(&sc->dev);

    if (val & (2 << channel)) {
        if (!(val & 0x20)) {
            _i8254_latch_count(sc);
        }
        if (!(val & 0x10) && !sc->status_latched) {
            uint64_t ts;

            /* status latch */
            /* XXX: add BCD and null count */
            ts = clock_get_ns(sc->dev.clock);

            sc->status =
                (_i8254_get_out(sc, ts) << 7) |
                (sc->rw_mode << 4) |
                (sc->mode << 1) |
                sc->bcd;
            sc->status_latched = 1;
        }
    }

    device_unlock(&sc->dev);
}

static void i8254_channel_io_write_3(struct i8254_channel *sc, uint32_t val)
{
    int access;

    device_lock(&sc->dev);

    access = (val >> 4) & 3;
    if (access == 0) {
        _i8254_latch_count(sc);
    } else {
        sc->rw_mode = access;
        sc->read_state = access;
        sc->write_state = access;

        sc->mode = (val >> 1) & 7;
        sc->bcd = val & 1;
        /* XXX: update irq timer ? */
    }

    device_unlock(&sc->dev);
}

static void i8254_channel_io_write(struct i8254_channel *sc, uint32_t val)
{
    device_lock(&sc->dev);

    switch (sc->write_state) {
    default:
    case RW_STATE_LSB:
        _i8254_load_count(sc, val);
        break;
    case RW_STATE_MSB:
        _i8254_load_count(sc, val << 8);
        break;
    case RW_STATE_WORD0:
        sc->write_latch = val;
        sc->write_state = RW_STATE_WORD1;
        break;
    case RW_STATE_WORD1:
        _i8254_load_count(sc, sc->write_latch | (val << 8));
        sc->write_state = RW_STATE_WORD0;
        break;
    }

    device_unlock(&sc->dev);
}

void i8254_io_write(struct i8254 *s, uint32_t addr, uint32_t val)
{
    int channel;

    addr &= 3;
    if (addr == 3) {
        channel = val >> 6;
        if (channel == 3) {
            /* read back command */
            for (channel = 0; channel < 3; channel++) {
                i8254_channel_io_write_rb(&s->channels[channel], val, channel);
            }
        } else {
            i8254_channel_io_write_3(&s->channels[channel], val);
        }
    } else {
        i8254_channel_io_write(&s->channels[addr], val);
    }
}

static uint32_t i8254_channel_io_read(struct i8254_channel *sc)
{
    int ret, count;

    device_lock(&sc->dev);

    if (sc->status_latched) {
        sc->status_latched = 0;
        ret = sc->status;
    } else if (sc->count_latched) {
        switch (sc->count_latched) {
        default:
        case RW_STATE_LSB:
            ret = sc->latched_count & 0xff;
            sc->count_latched = 0;
            break;
        case RW_STATE_MSB:
            ret = sc->latched_count >> 8;
            sc->count_latched = 0;
            break;
        case RW_STATE_WORD0:
            ret = sc->latched_count & 0xff;
            sc->count_latched = RW_STATE_MSB;
            break;
        }
    } else {
        switch (sc->read_state) {
        default:
        case RW_STATE_LSB:
            count = _i8254_get_count(sc);
            ret = count & 0xff;
            break;
        case RW_STATE_MSB:
            count = _i8254_get_count(sc);
            ret = (count >> 8) & 0xff;
            break;
        case RW_STATE_WORD0:
            count = _i8254_get_count(sc);
            ret = count & 0xff;
            sc->read_state = RW_STATE_WORD1;
            break;
        case RW_STATE_WORD1:
            count = _i8254_get_count(sc);
            ret = (count >> 8) & 0xff;
            sc->read_state = RW_STATE_WORD0;
            break;
        }
    }

    device_unlock(&sc->dev);

    return ret;
}

uint32_t i8254_io_read(struct i8254 *s, uint32_t addr)
{
    uint32_t ret = -1U;

    addr &= 3;
    if (addr < 3) {
        ret = i8254_channel_io_read(&s->channels[addr]);
    }

    return ret;
}

static void i8254_irq_timer(struct timer *timer)
{
    struct i8254_channel *sc;

    sc = container_of(timer, struct i8254_channel, irq_timer);

    device_lock(&sc->dev);
    _i8254_irq_timer_update(sc, sc->next_transition_time);
    device_unlock(&sc->dev);
}

static void i8254_channel_reset(struct i8254_channel *sc, int i)
{
    device_lock(&sc->dev);

    sc->mode = 3;
    sc->gate = (i != 2);
    sc->count_load_time = clock_get_ns(sc->dev.clock);
    sc->count = 0x10000;
    if (i == 0 && !sc->irq_disabled) {
        sc->next_transition_time =
            _i8254_get_next_transition_time(sc, sc->count_load_time);

        timer_set_deadline_ns(&sc->irq_timer, sc->next_transition_time);
    }

    device_unlock(&sc->dev);
}

static void _i8254_reset(struct i8254 *s)
{
    int i;

    for (i = 0; i < 3; i++) {
        i8254_channel_reset(&s->channels[i], i);
    }
}

void i8254_reset(struct i8254 *i8254)
{
    device_lock(&i8254->dev);
    _i8254_reset(i8254);
    device_unlock(&i8254->dev);
}

static void i8254_channel_set_irq_enable(struct i8254_channel *sc, int enable)
{
    device_lock(&sc->dev);

    if (enable) {
        sc->irq_disabled = 0;
        _i8254_irq_timer_update(sc, clock_get_ns(sc->dev.clock));
    } else {
        sc->irq_disabled = 1;
        timer_cancel(&sc->irq_timer);
    }

    device_unlock(&sc->dev);
}

/* When HPET is operating in legacy mode, suppress the ignored timer IRQ,
 * reenable it when legacy mode is left again. */
void i8254_set_irq_enable(struct i8254 *s, int enable)
{
    i8254_channel_set_irq_enable(&s->channels[0], enable);
}

static void i8254_channel_init(struct i8254_channel *sc, struct clock *c,
                               int index)
{
    device_init(&sc->dev, c, "channel[%d]", index);

    /* the timer 0 is connected to an IRQ */
    if (index == 0) {
        device_init_timer(&sc->dev, &sc->irq_timer,
                          i8254_irq_timer, "irq_timer");
        device_init_pin(&sc->dev, &sc->irq, "irq");
        sc->irq_disabled = 0;
    } else {
        sc->irq_disabled = 1;
    }
}

static void i8254_channel_cleanup(struct device *dev, void *opaque)
{
    struct i8254_channel *sc = opaque;

    device_cleanup(&sc->dev);
}

void i8254_init(struct i8254 *s, struct clock *c)
{
    int i;

    device_init(&s->dev, c, "i8254");

    for (i = 0; i < 3; i++) {
        i8254_channel_init(&s->channels[i], c, i);
        device_add_cleanup_handler(&s->dev, i8254_channel_cleanup,
                                   &s->channels[i]);
    }

    _i8254_reset(s);
}

void i8254_cleanup(struct i8254 *s)
{
    device_cleanup(&s->dev);
}
