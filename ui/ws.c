/*
 * QEMU VNC display driver
 *
 * Copyright (C) 2010 Anthony Liguori <anthony@codemonkey.ws>
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

#include "ws.h"
#include "md5.h"
#include "qemu_socket.h"

static void parse_key(const char *key, int *pws, uint32_t *pvalue)
{
    uint32_t value = 0;
    int i, ws = 0;

    for (i = 0; key[i]; i++) {
        if (key[i] >= '0' && key[i] <= '9') {
            value *= 10;
            value += (key[i] - '0');
        } else if (key[i] == ' ') {
            ws++;
        }
    }

    *pws = ws;
    *pvalue = value;
}

int ws_compute_challenge(const char *key1,
                         const char *key2,
                         const uint8_t *challenge,
                         uint8_t *response)
{
    uint32_t key1_value = 0, key2_value = 0;
    int key1_ws = 0, key2_ws = 0;
    uint8_t intermediate[16];

    parse_key(key1, &key1_ws, &key1_value);
    parse_key(key2, &key2_ws, &key2_value);

    if (!key1_ws || !key2_ws) {
        return 0;
    }

    key1_value = htonl(key1_value / key1_ws);
    key2_value = htonl(key2_value / key2_ws);

    memcpy(&intermediate[0], &key1_value, 4);
    memcpy(&intermediate[4], &key2_value, 4);
    memcpy(&intermediate[8], challenge, 8);

    md5_buffer((const char *)intermediate, 16, response);

    return 1;
}
