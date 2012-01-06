/*
 * Bounds Checked Buffer
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/safe-buffer.h"

#include <glib.h>

#define g_assert_no_overflow(lhs, rhs) \
    g_assert(((lhs) + (rhs)) >= (lhs) && ((lhs) + (rhs)) >= (rhs))

void sb_init(SafeBuffer *b, size_t initial_size)
{
    b->data = NULL;
    b->size = 0;
    b->capacity = 0;
    b->ref = 1;
    b->floating = true;
    b->foreign = false;

    sb_set_size(b, initial_size);
}

void sb_init_foreign(SafeBuffer *b, void *data, size_t size)
{
    b->data = data;
    b->size = size;
    b->capacity = size;
    b->ref = 1;
    b->floating = true;
    b->foreign = true;
}

SafeBuffer *sb_new(size_t initial_size)
{
    SafeBuffer *b = g_malloc(sizeof(*b));

    sb_init(b, initial_size);
    b->floating = false;

    return b;
}

void sb_ref(SafeBuffer *b)
{
    b->ref++;
}

void sb_unref(SafeBuffer *b)
{
    g_assert(b->ref > 0);

    if (--b->ref == 0) {
        if (!b->foreign) {
            g_free(b->data);
        }

        if (!b->floating) {
            g_free(b);
        }
    }
}

void sb_set_size(SafeBuffer *b, size_t size)
{
    g_assert(b->ref > 0);
    g_assert(!b->foreign);

    if (size > b->capacity) {
        b->capacity = (size * 2);
        b->data = g_realloc(b->data, b->capacity);
    }

    if (size > b->size) {
        memset(b->data + b->size, 0, size - b->size);
    }

    b->size = size;
}

void *sb_get_ptr(SafeBuffer *b, size_t offset, size_t len)
{
    g_assert(b->ref > 0);
    g_assert_no_overflow(offset, len);
    g_assert((offset + len) <= b->size);

    return b->data + offset;
}

const void *sb_get_ptr_ro(const SafeBuffer *b, size_t offset, size_t len)
{
    return sb_get_ptr((SafeBuffer *)b, offset, len);
}

void sb_copy_to(SafeBuffer *b, size_t offset,
                const void *data, size_t size)
{
    g_assert(b->ref > 0);
    memmove(sb_get_ptr(b, offset, size), data, size);
}

void sb_copy_from(void *data, size_t data_capacity,
                  const SafeBuffer *b, size_t offset,
                  size_t size)
{
    g_assert(b->ref > 0);
    memmove(data, sb_get_ptr_ro(b, offset, size), size);
}

void sb_copy(SafeBuffer *lhs, const SafeBuffer *rhs, size_t size)
{
    sb_copy_offset(lhs, 0, rhs, 0, size);
}

void sb_copy_offset(SafeBuffer *lhs, size_t lhs_offset,
                    const SafeBuffer *rhs, size_t rhs_offset,
                    size_t size)
{
    memmove(sb_get_ptr(lhs, lhs_offset, size),
            sb_get_ptr_ro(rhs, rhs_offset, size),
            size);
}

void sb_append(SafeBuffer *b, const void *data, size_t size)
{
    size_t offset = b->size;

    g_assert(b->ref > 0);
    g_assert_no_overflow(b->size, size);

    sb_set_size(b, b->size + size);
    sb_copy_to(b, offset, data, size);
}
