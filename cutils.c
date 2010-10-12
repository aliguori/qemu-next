/*
 * Simple C functions to supplement the C library
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include "host-utils.h"

void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

/* strcat and truncate. */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
    if (len < buf_size)
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}

int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (*p != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

int stristart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (qemu_toupper(*p) != qemu_toupper(*q))
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

/* XXX: use host strnlen if available ? */
int qemu_strnlen(const char *s, int max_len)
{
    int i;

    for(i = 0; i < max_len; i++) {
        if (s[i] == '\0') {
            break;
        }
    }
    return i;
}

time_t mktimegm(struct tm *tm)
{
    time_t t;
    int y = tm->tm_year + 1900, m = tm->tm_mon + 1, d = tm->tm_mday;
    if (m < 3) {
        m += 12;
        y--;
    }
    t = 86400 * (d + (153 * m - 457) / 5 + 365 * y + y / 4 - y / 100 + 
                 y / 400 - 719469);
    t += 3600 * tm->tm_hour + 60 * tm->tm_min + tm->tm_sec;
    return t;
}

int qemu_fls(int i)
{
    return 32 - clz32(i);
}

/*
 * Make sure data goes on disk, but if possible do not bother to
 * write out the inode just for timestamp updates.
 *
 * Unfortunately even in 2009 many operating systems do not support
 * fdatasync and have to fall back to fsync.
 */
int qemu_fdatasync(int fd)
{
#ifdef CONFIG_FDATASYNC
    return fdatasync(fd);
#else
    return fsync(fd);
#endif
}

/* io vectors */

void qemu_iovec_init(QEMUIOVector *qiov, int alloc_hint)
{
    qiov->iov = qemu_malloc(alloc_hint * sizeof(struct iovec));
    qiov->niov = 0;
    qiov->nalloc = alloc_hint;
    qiov->size = 0;
}

void qemu_iovec_init_external(QEMUIOVector *qiov, struct iovec *iov, int niov)
{
    int i;

    qiov->iov = iov;
    qiov->niov = niov;
    qiov->nalloc = -1;
    qiov->size = 0;
    for (i = 0; i < niov; i++)
        qiov->size += iov[i].iov_len;
}

void qemu_iovec_add(QEMUIOVector *qiov, void *base, size_t len)
{
    assert(qiov->nalloc != -1);

    if (qiov->niov == qiov->nalloc) {
        qiov->nalloc = 2 * qiov->nalloc + 1;
        qiov->iov = qemu_realloc(qiov->iov, qiov->nalloc * sizeof(struct iovec));
    }
    qiov->iov[qiov->niov].iov_base = base;
    qiov->iov[qiov->niov].iov_len = len;
    qiov->size += len;
    ++qiov->niov;
}

/*
 * Copies iovecs from src to the end of dst. It starts copying after skipping
 * the given number of bytes in src and copies until src is completely copied
 * or the total size of the copied iovec reaches size.The size of the last
 * copied iovec is changed in order to fit the specified total size if it isn't
 * a perfect fit already.
 */
void qemu_iovec_copy(QEMUIOVector *dst, QEMUIOVector *src, uint64_t skip,
    size_t size)
{
    int i;
    size_t done;
    void *iov_base;
    uint64_t iov_len;

    assert(dst->nalloc != -1);

    done = 0;
    for (i = 0; (i < src->niov) && (done != size); i++) {
        if (skip >= src->iov[i].iov_len) {
            /* Skip the whole iov */
            skip -= src->iov[i].iov_len;
            continue;
        } else {
            /* Skip only part (or nothing) of the iov */
            iov_base = (uint8_t*) src->iov[i].iov_base + skip;
            iov_len = src->iov[i].iov_len - skip;
            skip = 0;
        }

        if (done + iov_len > size) {
            qemu_iovec_add(dst, iov_base, size - done);
            break;
        } else {
            qemu_iovec_add(dst, iov_base, iov_len);
        }
        done += iov_len;
    }
}

void qemu_iovec_concat(QEMUIOVector *dst, QEMUIOVector *src, size_t size)
{
    qemu_iovec_copy(dst, src, 0, size);
}

void qemu_iovec_destroy(QEMUIOVector *qiov)
{
    assert(qiov->nalloc != -1);

    qemu_free(qiov->iov);
}

void qemu_iovec_reset(QEMUIOVector *qiov)
{
    assert(qiov->nalloc != -1);

    qiov->niov = 0;
    qiov->size = 0;
}

void qemu_iovec_to_buffer(QEMUIOVector *qiov, void *buf)
{
    uint8_t *p = (uint8_t *)buf;
    int i;

    for (i = 0; i < qiov->niov; ++i) {
        memcpy(p, qiov->iov[i].iov_base, qiov->iov[i].iov_len);
        p += qiov->iov[i].iov_len;
    }
}

void qemu_iovec_from_buffer(QEMUIOVector *qiov, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t copy;
    int i;

    for (i = 0; i < qiov->niov && count; ++i) {
        copy = count;
        if (copy > qiov->iov[i].iov_len)
            copy = qiov->iov[i].iov_len;
        memcpy(qiov->iov[i].iov_base, p, copy);
        p     += copy;
        count -= copy;
    }
}

void qemu_iovec_memset(QEMUIOVector *qiov, int c, size_t count)
{
    size_t n;
    int i;

    for (i = 0; i < qiov->niov && count; ++i) {
        n = MIN(count, qiov->iov[i].iov_len);
        memset(qiov->iov[i].iov_base, c, n);
        count -= n;
    }
}

#ifndef _WIN32
/* Sets a specific flag */
int fcntl_setfl(int fd, int flag)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        return -errno;

    if (fcntl(fd, F_SETFL, flags | flag) == -1)
        return -errno;

    return 0;
}
#endif

#define EXABYTES(x)     ((long long)(x) << 60)
#define PETABYTES(x)    ((long long)(x) << 50)
#define TERABYTES(x)    ((long long)(x) << 40)
#define GIGABYTES(x)    ((long long)(x) << 30)
#define MEGABYTES(x)    ((long long)(x) << 20)
#define KILOBYTES(x)    ((long long)(x) << 10)

long long cvtnum(char *s)
{
    long long i;
    char *sp;
    int c;

    i = strtoll(s, &sp, 0);
    if (i == 0 && sp == s) {
        return -1LL;
    }
    if (*sp == '\0') {
        return i;
    }

    if (sp[1] != '\0') {
        return -1LL;
    }

    c = tolower(*sp);
    switch (c) {
        default:
            return i;
        case 'k':
            return KILOBYTES(i);
        case 'm':
            return MEGABYTES(i);
        case 'g':
            return GIGABYTES(i);
        case 't':
            return TERABYTES(i);
        case 'p':
            return PETABYTES(i);
        case 'e':
            return  EXABYTES(i);
    }
    return -1LL;
}

#define TO_EXABYTES(x)     ((x) / EXABYTES(1))
#define TO_PETABYTES(x)    ((x) / PETABYTES(1))
#define TO_TERABYTES(x)    ((x) / TERABYTES(1))
#define TO_GIGABYTES(x)    ((x) / GIGABYTES(1))
#define TO_MEGABYTES(x)    ((x) / MEGABYTES(1))
#define TO_KILOBYTES(x)    ((x) / KILOBYTES(1))

void cvtstr(double value, char *str, size_t size)
{
    const char *fmt;
    int precise;

    precise = ((double)value * 1000 == (double)(int)value * 1000);

    if (value >= EXABYTES(1)) {
        fmt = precise ? "%.f EiB" : "%.3f EiB";
        snprintf(str, size, fmt, TO_EXABYTES(value));
    } else if (value >= PETABYTES(1)) {
        fmt = precise ? "%.f PiB" : "%.3f PiB";
        snprintf(str, size, fmt, TO_PETABYTES(value));
    } else if (value >= TERABYTES(1)) {
        fmt = precise ? "%.f TiB" : "%.3f TiB";
        snprintf(str, size, fmt, TO_TERABYTES(value));
    } else if (value >= GIGABYTES(1)) {
        fmt = precise ? "%.f GiB" : "%.3f GiB";
        snprintf(str, size, fmt, TO_GIGABYTES(value));
    } else if (value >= MEGABYTES(1)) {
        fmt = precise ? "%.f MiB" : "%.3f MiB";
        snprintf(str, size, fmt, TO_MEGABYTES(value));
    } else if (value >= KILOBYTES(1)) {
        fmt = precise ? "%.f KiB" : "%.3f KiB";
        snprintf(str, size, fmt, TO_KILOBYTES(value));
    } else {
        snprintf(str, size, "%f bytes", value);
    }
}
