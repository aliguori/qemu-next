/*
 * QEMU 8253/8254 - internal interfaces
 *
 * Copyright (c) 2011 Jan Kiszka, Siemens AG
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

#ifndef I8254_H
#define I8254_H

#include "newemu/device.h"

struct i8254_channel
{
    struct device dev;

    struct timer irq_timer;
    struct pin irq;

    int64_t count_load_time;
    int64_t next_transition_time;

    uint32_t irq_disabled;
    int count; /* can be 65536 */

    uint16_t latched_count;

    uint8_t count_latched;
    uint8_t status_latched;
    uint8_t status;
    uint8_t read_state;
    uint8_t write_state;
    uint8_t write_latch;
    uint8_t rw_mode;
    uint8_t mode;
    uint8_t bcd; /* not supported */
    uint8_t gate; /* timer start */

    char reserved[32];
};

struct i8254
{
    struct device dev;

    struct i8254_channel channels[3];

    char reserved[32];
};


void i8254_init(struct i8254 *s, struct clock *c);

void i8254_cleanup(struct i8254 *s);

void i8254_set_channel_gate(struct i8254 *s, int channel, int val);

void i8254_io_write(struct i8254 *s, uint32_t addr, uint32_t val);

uint32_t i8254_io_read(struct i8254 *s, uint32_t addr);

void i8254_set_irq_enable(struct i8254 *s, int enable);

void i8254_reset(struct i8254 *s);

#endif
