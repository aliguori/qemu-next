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

#include "buffer.h"

#define BUG_ON(cond) assert(!(cond))

void buffer_init(Buffer *buffer)
{
    memset(buffer, 0, sizeof(*buffer));
}

uint8_t *buffer_ptr(Buffer *buf)
{
    return buf->buffer;
}

size_t buffer_length(Buffer *buf)
{
    return buf->offset;
}

void buffer_reserve(Buffer *buffer, size_t len)
{
    if ((buffer->capacity - buffer->offset) < len) {
        buffer->capacity += (len + 1024);
        buffer->buffer = qemu_realloc(buffer->buffer, buffer->capacity + 1);
    }
}

int buffer_empty(Buffer *buffer)
{
    return buffer->offset == 0;
}

uint8_t *buffer_end(Buffer *buffer)
{
    return buffer->buffer + buffer->offset;
}

void buffer_reset(Buffer *buffer)
{
    buffer->offset = 0;
}

void buffer_free(Buffer *buffer)
{
    qemu_free(buffer->buffer);
    buffer->offset = 0;
    buffer->capacity = 0;
    buffer->buffer = NULL;
}

void buffer_append(Buffer *buffer, const void *data, size_t len)
{
    buffer_reserve(buffer, len);
    memcpy(buffer->buffer + buffer->offset, data, len);
    buffer->offset += len;
    buffer->buffer[buffer->offset] = 0;
}

void buffer_advance(Buffer *buf, size_t len)
{
    memmove(buf->buffer, buf->buffer + len,
            (buf->offset - len));
    buf->offset -= len;
}

static const char b64_table[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
};

void buffer_append_b64enc(Buffer *buf, const void *payload, size_t size)
{
    int i;

    for (i = 0; i < size; i += 3) {
        uint8_t dest[4];
        uint8_t ptr[3];
        int len = MIN(size - i, 3);
        uint8_t val;

        memset(ptr, 0, sizeof(ptr));
        memcpy(ptr, payload + i, len);

        val = (ptr[0] >> 2) & 0x3F;
        dest[0] = b64_table[val];

        val = ((ptr[0] & 0x03) << 4) | ((ptr[1] & 0xF0) >> 4);
        dest[1] = b64_table[val];

        if (len > 1) {
            val = ((ptr[1] & 0x0F) << 2) | ((ptr[2] & 0xC0) >> 6);
            dest[2] = b64_table[val];
        } else {
            dest[2] = '=';
        }

        if (len > 2) {
            val = ptr[2] & 0x3F;
            dest[3] = b64_table[val];
        } else {
            dest[3] = '=';
        }

        buffer_append(buf, dest, 4);
    }
}

static uint8_t b64dec_ch(uint8_t ch)
{
    switch (ch) {
    case 'A' ... 'Z':
        return (ch - 'A');
    case 'a' ... 'z':
        return (ch - 'a') + 26;
    case '0' ... '9':
        return (ch - '0') + 52;
    case '+':
        return 62;
    case '/':
        return 63;
    case '=':
    default:
        /* FIXME error */
        return 0;
    }
}

void buffer_append_b64dec(Buffer *buf, const void *payload, size_t size)
{
    int i;

    BUG_ON((size % 4) != 0);

    for (i = 0; i < size; i += 4) {
        uint8_t src[4];
        uint8_t dest[3];
        uint8_t val;
        int len;

        memcpy(src, payload + i, 4);

        val = b64dec_ch(src[0]);
        dest[0] = (val << 2);

        val = b64dec_ch(src[1]);
        dest[0] |= (val & 0x30) >> 4;
        dest[1] = (val & 0x0F) << 4;

        val = b64dec_ch(src[2]);
        dest[1] |= (val & 0x3C) >> 2;
        dest[2] = (val & 0x03) << 6;

        val = b64dec_ch(src[3]);
        dest[2] |= (val & 0x3F);

        if (src[2] == '=') {
            len = 1;
        } else if (src[3] == '=') {
            len = 2;
        } else {
            len = 3;
        }

        buffer_append(buf, dest, len);
    }
}
