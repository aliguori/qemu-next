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

#define RW_STATE_LSB 1
#define RW_STATE_MSB 2
#define RW_STATE_WORD0 3
#define RW_STATE_WORD1 4

#define I8254_FREQ 1193182

static inline uint64_t muldiv64(uint64_t a, uint64_t b, uint64_t c)
{
    return (a * b) / c;
}

static int _i8254_get_count(struct i8254_channel *s)
{
    uint64_t d;
    int counter;

    d = muldiv64(clock_get_ns(s->dev.clock) - s->count_load_time, I8254_FREQ,
                 NS_PER_SEC);
    switch (s->mode) {
    case 0:
    case 1:
    case 4:
    case 5:
        counter = (s->count - d) & 0xffff;
        break;
    case 3:
        /* XXX: may be incorrect for odd counts */
        counter = s->count - ((2 * d) % s->count);
        break;
    default:
        counter = s->count - (d % s->count);
        break;
    }

    return counter;
}

/* return -1 if no transition will occur.  */
static int64_t _i8254_get_next_transition_time(struct i8254_channel *s,
                                               int64_t current_time)
{
    uint64_t d, next_time, base;
    int period2;

    d = muldiv64(current_time - s->count_load_time, I8254_FREQ, NS_PER_SEC);
    switch (s->mode) {
    default:
    case 0:
    case 1:
        if (d < s->count) {
            next_time = s->count;
        } else {
            return -1;
        }
        break;
    case 2:
        base = (d / s->count) * s->count;
        if ((d - base) == 0 && d != 0) {
            next_time = base + s->count;
        } else {
            next_time = base + s->count + 1;
        }
        break;
    case 3:
        base = (d / s->count) * s->count;
        period2 = ((s->count + 1) >> 1);
        if ((d - base) < period2) {
            next_time = base + period2;
        } else {
            next_time = base + s->count;
        }
        break;
    case 4:
    case 5:
        if (d < s->count) {
            next_time = s->count;
        } else if (d == s->count) {
            next_time = s->count + 1;
        } else {
            return -1;
        }
        break;
    }
    /* convert to timer units */
    next_time = s->count_load_time + muldiv64(next_time, NS_PER_SEC,
                                              I8254_FREQ);
    /* fix potential rounding problems */
    /* XXX: better solution: use a clock at I8254_FREQ Hz */
    if (next_time <= current_time) {
        next_time = current_time + 1;
    }
    return next_time;
}

/* get i8254 output bit */
static int _i8254_get_out(struct i8254_channel *s, int64_t current_time)
{
    uint64_t d;
    int out;

    d = muldiv64(current_time - s->count_load_time, I8254_FREQ, NS_PER_SEC);
    switch (s->mode) {
    default:
    case 0:
        out = (d >= s->count);
        break;
    case 1:
        out = (d < s->count);
        break;
    case 2:
        if ((d % s->count) == 0 && d != 0) {
            out = 1;
        } else {
            out = 0;
        }
        break;
    case 3:
        out = (d % s->count) < ((s->count + 1) >> 1);
        break;
    case 4:
    case 5:
        out = (d == s->count);
        break;
    }
    return out;
}

static void _i8254_irq_timer_update(struct i8254_channel *s, 
                                    int64_t current_time)
{
    int64_t expire_time;
    int irq_level;

    if (s->irq_disabled) {
        return;
    }

    expire_time = _i8254_get_next_transition_time(s, current_time);
    irq_level = _i8254_get_out(s, current_time);
    pin_set_level(&s->irq, irq_level);
    s->next_transition_time = expire_time;

    if (expire_time != -1) {
        timer_set_deadline_ns(&s->irq_timer, expire_time);
    } else {
        timer_cancel(&s->irq_timer);
    }
}

/* val must be 0 or 1 */
void i8254_set_channel_gate(struct i8254 *s, int channel, int val)
{
    struct i8254_channel *sc;

    device_lock(&s->dev);

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

    device_unlock(&s->dev);
}

static void _i8254_load_count(struct i8254_channel *s, int val)
{
    if (val == 0) {
        val = 0x10000;
    }
    s->count_load_time = clock_get_ns(s->dev.clock);
    s->count = val;
    _i8254_irq_timer_update(s, s->count_load_time);
}

/* if already latched, do not latch again */
static void _i8254_latch_count(struct i8254_channel *s)
{
    if (!s->count_latched) {
        s->latched_count = _i8254_get_count(s);
        s->count_latched = s->rw_mode;
    }
}

void i8254_io_write(struct i8254 *i8254, uint32_t addr, uint32_t val)
{
    int channel, access;
    struct i8254_channel *s;

    device_lock(&i8254->dev);

    addr &= 3;
    if (addr == 3) {
        channel = val >> 6;
        if (channel == 3) {
            /* read back command */
            for (channel = 0; channel < 3; channel++) {
                s = &i8254->channels[channel];
                if (val & (2 << channel)) {
                    if (!(val & 0x20)) {
                        _i8254_latch_count(s);
                    }
                    if (!(val & 0x10) && !s->status_latched) {
                        uint64_t ts;

                        /* status latch */
                        /* XXX: add BCD and null count */
                        ts = clock_get_ns(i8254->dev.clock);

                        s->status =
                            (_i8254_get_out(s, ts) << 7) |
                            (s->rw_mode << 4) |
                            (s->mode << 1) |
                            s->bcd;
                        s->status_latched = 1;
                    }
                }
            }
        } else {
            s = &i8254->channels[channel];
            access = (val >> 4) & 3;
            if (access == 0) {
                _i8254_latch_count(s);
            } else {
                s->rw_mode = access;
                s->read_state = access;
                s->write_state = access;

                s->mode = (val >> 1) & 7;
                s->bcd = val & 1;
                /* XXX: update irq timer ? */
            }
        }
    } else {
        s = &i8254->channels[addr];
        switch (s->write_state) {
        default:
        case RW_STATE_LSB:
            _i8254_load_count(s, val);
            break;
        case RW_STATE_MSB:
            _i8254_load_count(s, val << 8);
            break;
        case RW_STATE_WORD0:
            s->write_latch = val;
            s->write_state = RW_STATE_WORD1;
            break;
        case RW_STATE_WORD1:
            _i8254_load_count(s, s->write_latch | (val << 8));
            s->write_state = RW_STATE_WORD0;
            break;
        }
    }

    device_unlock(&i8254->dev);
}

uint32_t i8254_io_read(struct i8254 *i8254, uint32_t addr)
{
    int ret, count;
    struct i8254_channel *s;

    device_lock(&i8254->dev);

    addr &= 3;
    s = &i8254->channels[addr];
    if (s->status_latched) {
        s->status_latched = 0;
        ret = s->status;
    } else if (s->count_latched) {
        switch (s->count_latched) {
        default:
        case RW_STATE_LSB:
            ret = s->latched_count & 0xff;
            s->count_latched = 0;
            break;
        case RW_STATE_MSB:
            ret = s->latched_count >> 8;
            s->count_latched = 0;
            break;
        case RW_STATE_WORD0:
            ret = s->latched_count & 0xff;
            s->count_latched = RW_STATE_MSB;
            break;
        }
    } else {
        switch (s->read_state) {
        default:
        case RW_STATE_LSB:
            count = _i8254_get_count(s);
            ret = count & 0xff;
            break;
        case RW_STATE_MSB:
            count = _i8254_get_count(s);
            ret = (count >> 8) & 0xff;
            break;
        case RW_STATE_WORD0:
            count = _i8254_get_count(s);
            ret = count & 0xff;
            s->read_state = RW_STATE_WORD1;
            break;
        case RW_STATE_WORD1:
            count = _i8254_get_count(s);
            ret = (count >> 8) & 0xff;
            s->read_state = RW_STATE_WORD0;
            break;
        }
    }

    device_unlock(&i8254->dev);

    return ret;
}

static void i8254_irq_timer(struct timer *timer)
{
    struct i8254_channel *s;

    s = container_of(timer, struct i8254_channel, irq_timer);

    device_lock(&s->dev);
    _i8254_irq_timer_update(s, s->next_transition_time);
    device_unlock(&s->dev);
}

static void _i8254_reset(struct i8254 *i8254)
{
    struct i8254_channel *s;
    int i;

    for (i = 0; i < 3; i++) {
        s = &i8254->channels[i];
        s->mode = 3;
        s->gate = (i != 2);
        s->count_load_time = clock_get_ns(s->dev.clock);
        s->count = 0x10000;
        if (i == 0 && !s->irq_disabled) {
            s->next_transition_time =
                _i8254_get_next_transition_time(s, s->count_load_time);
        }
    }

    s = &i8254->channels[0];
    if (!s->irq_disabled) {
        timer_set_deadline_ns(&s->irq_timer, s->next_transition_time);
    }
}

void i8254_reset(struct i8254 *i8254)
{
    device_lock(&i8254->dev);
    _i8254_reset(i8254);
    device_unlock(&i8254->dev);
}

/* When HPET is operating in legacy mode, suppress the ignored timer IRQ,
 * reenable it when legacy mode is left again. */
void i8254_set_irq_enable(struct i8254 *pit, int enable)
{
    struct i8254_channel *s;

    device_lock(&pit->dev);

    s = &pit->channels[0];

    if (enable) {
        s->irq_disabled = 0;
        _i8254_irq_timer_update(s, clock_get_ns(s->dev.clock));
    } else {
        s->irq_disabled = 1;
        timer_cancel(&s->irq_timer);
    }

    device_unlock(&pit->dev);
}

static void i8254_channel_init(struct i8254_channel *s, struct clock *c,
                               int index)
{
    device_init(&s->dev, c, "channel[%d]", index);

    /* the timer 0 is connected to an IRQ */
    if (index == 0) {
        device_init_timer(&s->dev, &s->irq_timer, i8254_irq_timer, "irq_timer");
    } else {
        s->irq_disabled = 1;
    }
}

static void i8254_channel_cleanup(struct device *dev, void *opaque)
{
    struct i8254_channel *s = opaque;
    device_cleanup(&s->dev);
}

void i8254_init(struct i8254 *pit, struct clock *c)
{
    int i;

    device_init(&pit->dev, c, "i8254");

    for (i = 0; i < 3; i++) {
        i8254_channel_init(&pit->channels[i], c, i);
        device_add_cleanup_handler(&pit->dev, i8254_channel_cleanup,
                                   &pit->channels[i]);
    }

    _i8254_reset(pit);
}

void i8254_cleanup(struct i8254 *pit)
{
    device_cleanup(&pit->dev);
}
