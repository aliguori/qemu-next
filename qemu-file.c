/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu_socket.h"
#include "hw/hw.h"
#include "qapi/qemu-file-output-visitor.h"
#include "qapi/qemu-file-input-visitor.h"
#include "trace.h"

#define IO_BUF_SIZE 32768

struct QEMUFile {
    QEMUFilePutBufferFunc *put_buffer;
    QEMUFileGetBufferFunc *get_buffer;
    QEMUFileCloseFunc *close;
    QEMUFileRateLimit *rate_limit;
    QEMUFileSetRateLimit *set_rate_limit;
    QEMUFileGetRateLimit *get_rate_limit;
    void *opaque;
    int is_write;

    int64_t buf_offset; /* start of buffer when writing, end of buffer
                           when reading */
    int buf_index;
    int buf_size; /* 0 when writing */
    uint8_t buf[IO_BUF_SIZE];

    int last_error;
};

typedef struct QEMUFileStdio
{
    FILE *stdio_file;
    QEMUFile *file;
} QEMUFileStdio;

typedef struct QEMUFileSocket
{
    int fd;
    QEMUFile *file;
} QEMUFileSocket;

/* TODO: temporary mechanism to support existing function signatures by
 * creating a 1-to-1 mapping between QEMUFile's and the actual Visitor type.
 * In the case of QemuFileOutputVisitor/QemuFileInputVisitor, the QEMUFile
 * arg corresponds to the handle used by the visitor for reads/writes. For
 * other visitors, the QEMUFile will serve purely as a key.
 *
 * Once all interfaces are converted to using Visitor directly, we will
 * remove this lookup logic and pass the Visitor to the registered save/load
 * functions directly.
 */

/* Clean up a *Visitor object associated with the QEMUFile */
typedef int (QEMUFileVisitorCleanupFunc)(void *opaque);

typedef struct VisitorNode {
    void *opaque;
    Visitor *visitor;
    QEMUFile *file;
    QEMUFileVisitorCleanupFunc *cleanup;
    QTAILQ_ENTRY(VisitorNode) entry;
} VisitorNode;

static QTAILQ_HEAD(, VisitorNode) qemu_file_visitors =
    QTAILQ_HEAD_INITIALIZER(qemu_file_visitors);

Visitor *qemu_file_get_visitor(QEMUFile *f)
{
    VisitorNode *vnode;
    QTAILQ_FOREACH(vnode, &qemu_file_visitors, entry) {
        if (vnode->file == f) {
            return vnode->visitor;
        }
    }
    /* all QEMUFile instances should have an associated visitor */
    assert(false);
}

QEMUFile *qemu_file_from_visitor(Visitor *v)
{
    VisitorNode *vnode;
    QTAILQ_FOREACH(vnode, &qemu_file_visitors, entry) {
        if (vnode->visitor == v) {
            return vnode->file;
        }
    }
    return NULL;
}

static void qemu_file_put_visitor(QEMUFile *f, Visitor *v, void *opaque,
                                  QEMUFileVisitorCleanupFunc *cleanup)
{
    VisitorNode *vnode = g_malloc0(sizeof(*vnode));
    vnode->file = f;
    vnode->visitor = v;
    vnode->opaque = opaque;
    vnode->cleanup = cleanup;
    QTAILQ_INSERT_TAIL(&qemu_file_visitors, vnode, entry);
}

static void qemu_file_remove_visitor(QEMUFile *f)
{
    VisitorNode *vnode;
    QTAILQ_FOREACH(vnode, &qemu_file_visitors, entry) {
        if (vnode->file == f) {
            QTAILQ_REMOVE(&qemu_file_visitors, vnode, entry);
            if (vnode->cleanup) {
                vnode->cleanup(vnode->opaque);
            }
            g_free(vnode);
        }
    }
}

static int qemu_file_cleanup_output_visitor(void *opaque)
{
    QemuFileOutputVisitor *v = opaque;
    qemu_file_output_visitor_cleanup(v);
    return 0;
}

static int qemu_file_cleanup_input_visitor(void *opaque)
{
    QemuFileInputVisitor *v = opaque;
    qemu_file_input_visitor_cleanup(v);
    return 0;
}


static int socket_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileSocket *s = opaque;
    ssize_t len;

    do {
        len = qemu_recv(s->fd, buf, size, 0);
    } while (len == -1 && socket_error() == EINTR);

    if (len == -1) {
        len = -socket_error();
    }

    return len;
}

static int socket_close(void *opaque)
{
    QEMUFileSocket *s = opaque;
    g_free(s);
    return 0;
}

static int stdio_put_buffer(void *opaque, const uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    return fwrite(buf, 1, size, s->stdio_file);
}

static int stdio_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    FILE *fp = s->stdio_file;
    int bytes;

    do {
        clearerr(fp);
        bytes = fread(buf, 1, size, fp);
    } while ((bytes == 0) && ferror(fp) && (errno == EINTR));
    return bytes;
}

static int stdio_pclose(void *opaque)
{
    QEMUFileStdio *s = opaque;
    int ret;
    ret = pclose(s->stdio_file);
    g_free(s);
    return ret;
}

static int stdio_fclose(void *opaque)
{
    QEMUFileStdio *s = opaque;
    fclose(s->stdio_file);
    g_free(s);
    return 0;
}

QEMUFile *qemu_popen(FILE *stdio_file, const char *mode)
{
    QEMUFileStdio *s;

    if (stdio_file == NULL || mode == NULL || (mode[0] != 'r' && mode[0] != 'w') || mode[1] != 0) {
        fprintf(stderr, "qemu_popen: Argument validity check failed\n");
        return NULL;
    }

    s = g_malloc0(sizeof(QEMUFileStdio));

    s->stdio_file = stdio_file;

    if (mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, NULL, stdio_get_buffer, stdio_pclose,
                                 NULL, NULL, NULL);
    } else {
        s->file = qemu_fopen_ops(s, stdio_put_buffer, NULL, stdio_pclose,
                                 NULL, NULL, NULL);
    }
    return s->file;
}

QEMUFile *qemu_popen_cmd(const char *command, const char *mode)
{
    FILE *popen_file;

    popen_file = popen(command, mode);
    if (popen_file == NULL) {
        return NULL;
    }

    return qemu_popen(popen_file, mode);
}

int qemu_stdio_fd(QEMUFile *f)
{
    QEMUFileStdio *p;
    int fd;

    p = (QEMUFileStdio *)f->opaque;
    fd = fileno(p->stdio_file);

    return fd;
}

QEMUFile *qemu_fdopen(int fd, const char *mode)
{
    QEMUFileStdio *s;

    if (mode == NULL ||
        (mode[0] != 'r' && mode[0] != 'w') ||
        mode[1] != 'b' || mode[2] != 0) {
        fprintf(stderr, "qemu_fdopen: Argument validity check failed\n");
        return NULL;
    }

    s = g_malloc0(sizeof(QEMUFileStdio));
    s->stdio_file = fdopen(fd, mode);
    if (!s->stdio_file) {
        goto fail;
    }

    if (mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, NULL, stdio_get_buffer, stdio_fclose,
                                 NULL, NULL, NULL);
    } else {
        s->file = qemu_fopen_ops(s, stdio_put_buffer, NULL, stdio_fclose,
                                 NULL, NULL, NULL);
    }
    return s->file;

fail:
    g_free(s);
    return NULL;
}

QEMUFile *qemu_fopen_socket(int fd)
{
    QEMUFileSocket *s = g_malloc0(sizeof(QEMUFileSocket));

    s->fd = fd;
    s->file = qemu_fopen_ops(s, NULL, socket_get_buffer, socket_close,
                             NULL, NULL, NULL);
    return s->file;
}

static int file_put_buffer(void *opaque, const uint8_t *buf,
                            int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    fseek(s->stdio_file, pos, SEEK_SET);
    return fwrite(buf, 1, size, s->stdio_file);
}

static int file_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    fseek(s->stdio_file, pos, SEEK_SET);
    return fread(buf, 1, size, s->stdio_file);
}

QEMUFile *qemu_fopen(const char *filename, const char *mode)
{
    QEMUFileStdio *s;

    if (mode == NULL ||
        (mode[0] != 'r' && mode[0] != 'w') ||
        mode[1] != 'b' || mode[2] != 0) {
        fprintf(stderr, "qemu_fopen: Argument validity check failed\n");
        return NULL;
    }

    s = g_malloc0(sizeof(QEMUFileStdio));

    s->stdio_file = fopen(filename, mode);
    if (!s->stdio_file) {
        goto fail;
    }

    if (mode[0] == 'w') {
        s->file = qemu_fopen_ops(s, file_put_buffer, NULL, stdio_fclose,
                                 NULL, NULL, NULL);
    } else {
        s->file = qemu_fopen_ops(s, NULL, file_get_buffer, stdio_fclose,
                                 NULL, NULL, NULL);
    }
    return s->file;
fail:
    g_free(s);
    return NULL;
}

QEMUFile *qemu_fopen_ops(void *opaque, QEMUFilePutBufferFunc *put_buffer,
                         QEMUFileGetBufferFunc *get_buffer,
                         QEMUFileCloseFunc *close,
                         QEMUFileRateLimit *rate_limit,
                         QEMUFileSetRateLimit *set_rate_limit,
                         QEMUFileGetRateLimit *get_rate_limit)
{
    QEMUFile *f;

    f = g_malloc0(sizeof(QEMUFile));

    f->opaque = opaque;
    f->put_buffer = put_buffer;
    f->get_buffer = get_buffer;
    f->close = close;
    f->rate_limit = rate_limit;
    f->set_rate_limit = set_rate_limit;
    f->get_rate_limit = get_rate_limit;
    f->is_write = 0;

    if (put_buffer) {
        QemuFileOutputVisitor *ov = qemu_file_output_visitor_new(f);
        qemu_file_put_visitor(f, qemu_file_output_get_visitor(ov), ov,
                              qemu_file_cleanup_output_visitor);
    } else {
        QemuFileInputVisitor *iv = qemu_file_input_visitor_new(f);
        qemu_file_put_visitor(f, qemu_file_input_get_visitor(iv), iv,
                              qemu_file_cleanup_input_visitor);
    }

    return f;
}

int qemu_file_get_error(QEMUFile *f)
{
    return f->last_error;
}

void qemu_file_set_error(QEMUFile *f, int ret)
{
    f->last_error = ret;
}

void qemu_fflush(QEMUFile *f)
{
    if (!f->put_buffer) {
        return;
    }

    if (f->is_write && f->buf_index > 0) {
        int len;

        len = f->put_buffer(f->opaque, f->buf, f->buf_offset, f->buf_index);
        if (len > 0) {
            f->buf_offset += f->buf_index;
        } else {
            f->last_error = -EINVAL;
        }
        f->buf_index = 0;
    }
}

static void qemu_fill_buffer(QEMUFile *f)
{
    int len;
    int pending;

    if (!f->get_buffer) {
        return;
    }

    if (f->is_write) {
        abort();
    }

    pending = f->buf_size - f->buf_index;
    if (pending > 0) {
        memmove(f->buf, f->buf + f->buf_index, pending);
    }
    f->buf_index = 0;
    f->buf_size = pending;

    len = f->get_buffer(f->opaque, f->buf + pending, f->buf_offset,
                        IO_BUF_SIZE - pending);
    if (len > 0) {
        f->buf_size += len;
        f->buf_offset += len;
    } else if (len != -EAGAIN) {
        f->last_error = len;
    }
}

int qemu_fclose(QEMUFile *f)
{
    int ret = 0;
    qemu_fflush(f);
    qemu_file_remove_visitor(f);
    if (f->close) {
        ret = f->close(f->opaque);
    }
    g_free(f);
    return ret;
}

void qemu_file_put_notify(QEMUFile *f)
{
    f->put_buffer(f->opaque, NULL, 0, 0);
}

void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, int size)
{
    int l;

    trace_qemu_put_buffer(f, buf, size);

    if (!f->last_error && f->is_write == 0 && f->buf_index > 0) {
        fprintf(stderr,
                "Attempted to write to buffer while read buffer is not empty\n");
        abort();
    }

    while (!f->last_error && size > 0) {
        l = IO_BUF_SIZE - f->buf_index;
        if (l > size) {
            l = size;
        }
        memcpy(f->buf + f->buf_index, buf, l);
        f->is_write = 1;
        f->buf_index += l;
        buf += l;
        size -= l;
        if (f->buf_index >= IO_BUF_SIZE) {
            qemu_fflush(f);
        }
    }
}

void qemu_put_byte(QEMUFile *f, int v)
{
    if (!f->last_error && f->is_write == 0 && f->buf_index > 0) {
        fprintf(stderr,
                "Attempted to write to buffer while read buffer is not empty\n");
        abort();
    }

    trace_qemu_put_byte(f, v & 0xff);
    f->buf[f->buf_index++] = v;
    f->is_write = 1;
    if (f->buf_index >= IO_BUF_SIZE) {
        qemu_fflush(f);
    }
}

void qemu_file_skip(QEMUFile *f, int size)
{
    if (f->buf_index + size <= f->buf_size) {
        f->buf_index += size;
    }
}

int qemu_peek_buffer(QEMUFile *f, uint8_t *buf, int size, size_t offset)
{
    int pending;
    int index;

    if (f->is_write) {
        abort();
    }

    index = f->buf_index + offset;
    pending = f->buf_size - index;
    if (pending < size) {
        qemu_fill_buffer(f);
        index = f->buf_index + offset;
        pending = f->buf_size - index;
    }

    if (pending <= 0) {
        return 0;
    }
    if (size > pending) {
        size = pending;
    }

    memcpy(buf, f->buf + index, size);
    return size;
}

int qemu_get_buffer(QEMUFile *f, uint8_t *buf, int size)
{
    int pending = size;
    int done = 0;

    trace_qemu_get_buffer(f, buf, size);

    while (pending > 0) {
        int res;

        res = qemu_peek_buffer(f, buf, pending, 0);
        if (res == 0) {
            return done;
        }
        qemu_file_skip(f, res);
        buf += res;
        pending -= res;
        done += res;
    }
    return done;
}

int qemu_peek_byte(QEMUFile *f, int offset)
{
    int index = f->buf_index + offset;

    if (f->is_write) {
        abort();
    }

    if (index >= f->buf_size) {
        qemu_fill_buffer(f);
        index = f->buf_index + offset;
        if (index >= f->buf_size) {
            return 0;
        }
    }
    return f->buf[index];
}

int qemu_get_byte(QEMUFile *f)
{
    int result;

    result = qemu_peek_byte(f, 0);
    qemu_file_skip(f, 1);
    trace_qemu_get_byte(f, result);
    return result;
}

int64_t qemu_ftell(QEMUFile *f)
{
    return f->buf_offset - f->buf_size + f->buf_index;
}

int64_t qemu_fseek(QEMUFile *f, int64_t pos, int whence)
{
    if (whence == SEEK_SET) {
        /* nothing to do */
    } else if (whence == SEEK_CUR) {
        pos += qemu_ftell(f);
    } else {
        /* SEEK_END not supported */
        return -1;
    }
    if (f->put_buffer) {
        qemu_fflush(f);
        f->buf_offset = pos;
    } else {
        f->buf_offset = pos;
        f->buf_index = 0;
        f->buf_size = 0;
    }
    return pos;
}

int qemu_file_rate_limit(QEMUFile *f)
{
    if (f->rate_limit) {
        return f->rate_limit(f->opaque);
    }

    return 0;
}

int64_t qemu_file_get_rate_limit(QEMUFile *f)
{
    if (f->get_rate_limit) {
        return f->get_rate_limit(f->opaque);
    }

    return 0;
}

int64_t qemu_file_set_rate_limit(QEMUFile *f, int64_t new_rate)
{
    /* any failed or completed migration keeps its state to allow probing of
     * migration data, but has no associated file anymore */
    if (f && f->set_rate_limit) {
        return f->set_rate_limit(f->opaque, new_rate);
    }

    return 0;
}

void qemu_put_be16(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
    trace_qemu_put_be16(f, v);
}

void qemu_put_be32(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 24);
    qemu_put_byte(f, v >> 16);
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
    trace_qemu_put_be32(f, v);
}

void qemu_put_be64(QEMUFile *f, uint64_t v)
{
    qemu_put_be32(f, v >> 32);
    qemu_put_be32(f, v);
    trace_qemu_put_be64(f, v);
}

unsigned int qemu_get_be16(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    trace_qemu_get_be16(f, v);
    return v;
}

unsigned int qemu_get_be32(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 24;
    v |= qemu_get_byte(f) << 16;
    v |= qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    trace_qemu_get_be32(f, v);
    return v;
}

uint64_t qemu_get_be64(QEMUFile *f)
{
    uint64_t v;
    v = (uint64_t)qemu_get_be32(f) << 32;
    v |= qemu_get_be32(f);
    trace_qemu_get_be64(f, v);
    return v;
}