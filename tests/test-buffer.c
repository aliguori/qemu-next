/*
 * QEMU Growable Binary Buffer
 *
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

static void readfile(FILE *f, Buffer *buf)
{
    while (!feof(f)) {
        char data[1024];
        size_t len;

        len = fread(data, 1, sizeof(data), f);
        buffer_append(buf, data, len);
    }
}

static void writefile(FILE *f, Buffer *buf)
{
    fwrite(buffer_ptr(buf), buffer_length(buf), 1, f);
    fflush(f);
}

static int buffer_cmp(Buffer *lhs, Buffer *rhs)
{
    size_t l, r;

    l = buffer_length(lhs);
    r = buffer_length(rhs);

    if (l < r) {
        return -1;
    } else if (l > r) {
        return 1;
    }

    return memcmp(buffer_ptr(lhs), buffer_ptr(rhs), l);
}

int main(int argc, char **argv)
{
    Buffer plain, encoded, decoded, external_encoded;
    FILE *f;
    int err;

    buffer_init(&plain);
    buffer_init(&encoded);
    buffer_init(&decoded);
    buffer_init(&external_encoded);

    readfile(stdin, &plain);

    f = fopen("/tmp/b64.plain", "w");
    writefile(f, &plain);
    fclose(f);

    err = system("echo -n $(cat /tmp/b64.plain | base64) | "
                 "sed -e's: ::g' > /tmp/b64.enc");
    if (err == -1) {
        return 1;
    }

    f = fopen("/tmp/b64.enc", "r");
    readfile(f, &external_encoded);
    fclose(f);

    buffer_append_b64enc(&encoded,
                         buffer_ptr(&plain),
                         buffer_length(&plain));

    if (buffer_cmp(&encoded, &external_encoded) != 0) {
        fprintf(stderr, "FAIL: encoding failed\n"
                "internal: %s\n"
                "external: %s\n",
                buffer_ptr(&encoded),
                buffer_ptr(&external_encoded));
        return 1;
    }

    buffer_append_b64dec(&decoded,
                         buffer_ptr(&encoded),
                         buffer_length(&encoded));

    if (buffer_cmp(&plain, &decoded) != 0) {
        fprintf(stderr, "FAIL: decoding failed\n"
                "plain: %s\n"
                "decoded: %s\n",
                buffer_ptr(&plain),
                buffer_ptr(&decoded));
        return 1;
    }

    buffer_free(&external_encoded);
    buffer_free(&decoded);
    buffer_free(&encoded);
    buffer_free(&plain);
    
    return 0;
}
