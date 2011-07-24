#ifndef CHAR_DRIVER_MEM_H
#define CHAR_DRIVER_MEM_H

#include "qemu/chrdrv.h"

/**
 * @MemoryCharDriver:
 *
 * A @CharDriver that stores all written data to memory.  This is useful for
 * interfacing directly with objects that expect to work with a @CharDriver.
 *
 * The accumulated data can be obtained as a QString.
 */
typedef struct MemoryCharDriver
{
    CharDriver parent;

    /* Private */
    size_t outbuf_size;
    size_t outbuf_capacity;
    uint8_t *outbuf;
} MemoryCharDriver;

#define TYPE_MEMORY_CHAR_DRIVER "memory-char-driver"
#define MEMORY_CHAR_DRIVER(obj) \
    TYPE_CHECK(MemoryCharDriver, obj, TYPE_MEMORY_CHAR_DRIVER)

void memory_char_driver_initialize(MemoryCharDriver *d, const char *id);
void memory_char_driver_finalize(MemoryCharDriver *d);

/**
 * @memory_char_driver_get_osize:
 *
 * Returns:  The size of the output buffer.
 */
size_t memory_char_driver_get_osize(MemoryCharDriver *d);

/**
 * @memory_char_driver_get_qs:
 *
 * Returns:  A QString representing the output buffer.  The reference ownership
 *           is transferred to the caller.
 */
QString *memory_char_driver_get_qs(MemoryCharDriver *d);

#endif
