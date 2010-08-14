/*
 * QEMU Growable Binary Buffer
 *
 * Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori <aliguori@us.ibm.com>
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

/* Growable binary buffers */

typedef struct Buffer
{
    size_t capacity;
    size_t offset;
    uint8_t *buffer;
} Buffer;

/* Initialize an allocated buffer */
void buffer_init(Buffer *buffer);

/* Reserve len bytes more space in the buffer */
void buffer_reserve(Buffer *buffer, size_t len);

/* Returns true if buffer contains no data */
int buffer_empty(Buffer *buffer);

/* deprecated, should be refactored */
uint8_t *buffer_end(Buffer *buffer);

/* Reset an allocated buffer */
void buffer_reset(Buffer *buffer);

/* Free memory associated with a buffer */
void buffer_free(Buffer *buffer);

/* Append data to the end of a buffer, increasing the capacity as necessary */
void buffer_append(Buffer *buffer, const void *data, size_t len);

/* Same as buffer_append but b64 encode data before adding */
void buffer_append_b64enc(Buffer *dest, const void *payload, size_t size);

/* Same as buffer_append but b64 decode data before adding */
void buffer_append_b64dec(Buffer *dest, const void *payload, size_t size);

/* Advance the buffer pointer len bytes.  If a buffer is n bytes in size before
 * this function is called, it will contain the data from len..n after this
 * call.
 */
void buffer_advance(Buffer *buf, size_t len);

/* Returns a pointer to the start of the buffer */
uint8_t *buffer_ptr(Buffer *buf);

/* Returns the current buffer length */
size_t buffer_length(Buffer *buf);

#endif
