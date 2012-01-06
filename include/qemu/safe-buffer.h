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
#ifndef QEMU_SAFE_BUFFER_H
#define QEMU_SAFE_BUFFER_H

#include <sys/types.h>
#include <stdbool.h>
#include "hw/hw.h"

typedef struct SafeBuffer
{
    void *data;
    size_t size;
    size_t capacity;
    int ref;
    bool foreign;
    bool floating;
} SafeBuffer;

#define SAFE_BUFFER_STATIC(name, size)        \
    struct {                                  \
        SafeBuffer name;                      \
        uint8_t qemu__static_ ## name [size]; \
    }

#define SAFE_BUFFER_STATIC_INIT(s, name)                  \
    sb_init_foreign(&(s)->name,                           \
                    (s)-> qemu__static_ ## name,          \
                    sizeof((s)-> qemu__static_ ## name))

void sb_init(SafeBuffer *b, size_t initial_size);

void sb_init_foreign(SafeBuffer *b, void *data, size_t size);

SafeBuffer *sb_new(size_t initial_size);

void sb_ref(SafeBuffer *b);

void sb_unref(SafeBuffer *b);

void sb_set_size(SafeBuffer *b, size_t size);

void *sb_get_ptr(SafeBuffer *b, size_t offset, size_t len);

const void *sb_get_ptr_ro(const SafeBuffer *b, size_t offset, size_t len);

void sb_copy_to(SafeBuffer *b, size_t offset,
                const void *data, size_t size);

void sb_copy_from(void *data, size_t data_capacity,
                  const SafeBuffer *b, size_t offset,
                  size_t size);

void sb_copy(SafeBuffer *lhs, const SafeBuffer *rhs, size_t size);

void sb_copy_offset(SafeBuffer *lhs, size_t lhs_offset,
                    const SafeBuffer *rhs, size_t rhs_offset,
                    size_t size);

void sb_append(SafeBuffer *b, const void *data, size_t size);

/** Big Endian Integer Accessors **/

static inline uint8_t sb_ldb_be(SafeBuffer *b, size_t offset)
{
    return ldub_p(sb_get_ptr(b, offset, 1));
}

static inline uint16_t sb_ldw_be(SafeBuffer *b, size_t offset)
{
    return lduw_be_p(sb_get_ptr(b, offset, 2));
}

static inline uint32_t sb_ldl_be(SafeBuffer *b, size_t offset)
{
    return ldl_be_p(sb_get_ptr(b, offset, 4));
}

static inline uint64_t sb_ldq_be(SafeBuffer *b, size_t offset)
{
    return ldq_be_p(sb_get_ptr(b, offset, 8));
}

static inline void sb_stb_be(SafeBuffer *b, size_t offset, uint8_t value)
{
    stb_p(sb_get_ptr(b, offset, 1), value);
}

static inline void sb_stw_be(SafeBuffer *b, size_t offset, uint16_t value)
{
    stw_be_p(sb_get_ptr(b, offset, 2), value);
}

static inline void sb_stl_be(SafeBuffer *b, size_t offset, uint32_t value)
{
    stl_be_p(sb_get_ptr(b, offset, 4), value);
}

static inline void sb_stq_be(SafeBuffer *b, size_t offset, uint64_t value)
{
    stq_be_p(sb_get_ptr(b, offset, 8), value);
}

/** Little Endian Integer Accessors **/

static inline uint8_t sb_ldb_le(SafeBuffer *b, size_t offset)
{
    return ldub_p(sb_get_ptr(b, offset, 1));
}

static inline uint16_t sb_ldw_le(SafeBuffer *b, size_t offset)
{
    return lduw_le_p(sb_get_ptr(b, offset, 2));
}

static inline uint32_t sb_ldl_le(SafeBuffer *b, size_t offset)
{
    return ldl_le_p(sb_get_ptr(b, offset, 4));
}

static inline uint64_t sb_ldq_le(SafeBuffer *b, size_t offset)
{
    return ldq_le_p(sb_get_ptr(b, offset, 8));
}

static inline void sb_stb_le(SafeBuffer *b, size_t offset, uint8_t value)
{
    stb_p(sb_get_ptr(b, offset, 1), value);
}

static inline void sb_stw_le(SafeBuffer *b, size_t offset, uint16_t value)
{
    stw_le_p(sb_get_ptr(b, offset, 2), value);
}

static inline void sb_stl_le(SafeBuffer *b, size_t offset, uint32_t value)
{
    stl_le_p(sb_get_ptr(b, offset, 4), value);
}

static inline void sb_stq_le(SafeBuffer *b, size_t offset, uint64_t value)
{
    stq_le_p(sb_get_ptr(b, offset, 8), value);
}

#endif
