/*
 * QEMU VNC display driver
 *
 * Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2009 Red Hat, Inc
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

#ifndef QEMU_BUFFER_H
#define QEMU_BUFFER_H

#include "qemu-common.h"

typedef struct Buffer
{
    size_t capacity;
    size_t offset;
    uint8_t *buffer;
} Buffer;

/* Buffer management */
void buffer_init(Buffer *buffer);
void buffer_reserve(Buffer *buffer, size_t len);
int buffer_empty(Buffer *buffer);
uint8_t *buffer_end(Buffer *buffer);
void buffer_reset(Buffer *buffer);
void buffer_free(Buffer *buffer);
void buffer_append(Buffer *buffer, const void *data, size_t len);
void buffer_advance(Buffer *buf, size_t len);
void buffer_append_b64enc(Buffer *dest, const void *payload, size_t size);
void buffer_append_b64dec(Buffer *dest, const void *payload, size_t size);

uint8_t *buffer_ptr(Buffer *buf);
size_t buffer_length(Buffer *buf);

#endif
