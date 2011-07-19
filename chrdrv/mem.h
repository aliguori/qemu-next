#ifndef CHAR_DRIVER_MEM_H
#define CHAR_DRIVER_MEM_H

#include "chrdrv.h"

/* Memory chardev */
typedef struct MemoryCharDriver
{
    CharDriver parent;

    size_t outbuf_size;
    size_t outbuf_capacity;
    uint8_t *outbuf;
} MemoryCharDriver;

#define TYPE_MEMORY_CHAR_DRIVER "memory-char-driver"
#define MEMORY_CHAR_DRIVER(obj) \
    TYPE_CHECK(MemoryCharDriver, obj, TYPE_MEMORY_CHAR_DRIVER)

void memory_char_driver_initialize(MemoryCharDriver *d, const char *id);
void memory_char_driver_finalize(MemoryCharDriver *d);

size_t memory_char_driver_get_osize(MemoryCharDriver *d);
QString *memory_char_driver_get_qs(MemoryCharDriver *d);

#endif
