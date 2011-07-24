#include "qemu/memchr.h"

void memory_char_driver_initialize(MemoryCharDriver *d, const char *id)
{
    type_initialize(d, TYPE_MEMORY_CHAR_DRIVER, id);
}

void memory_char_driver_finalize(MemoryCharDriver *d)
{
    type_finalize(d);
}

static int memory_char_driver_write(CharDriver *chr, const uint8_t *buf,
                                    int len)
{
    MemoryCharDriver *d = MEMORY_CHAR_DRIVER(chr);

    /* TODO: the QString implementation has the same code, we should
     * introduce a generic way to do this in cutils.c */
    if (d->outbuf_capacity < d->outbuf_size + len) {
        /* grow outbuf */
        d->outbuf_capacity += len;
        d->outbuf_capacity *= 2;
        d->outbuf = qemu_realloc(d->outbuf, d->outbuf_capacity);
    }

    memcpy(d->outbuf + d->outbuf_size, buf, len);
    d->outbuf_size += len;

    return len;
}

size_t memory_char_driver_get_osize(MemoryCharDriver *d)
{
    return d->outbuf_size;
}

QString *memory_char_driver_get_qs(MemoryCharDriver *d)
{
    return qstring_from_substr((char *) d->outbuf, 0, d->outbuf_size - 1);
}

static void memory_char_driver_fini(TypeInstance *inst)
{
    MemoryCharDriver *d = MEMORY_CHAR_DRIVER(inst);

    qemu_free(d->outbuf);
}

static void memory_char_driver_class_init(TypeClass *class)
{
    CharDriverClass *cdc = CHAR_DRIVER_CLASS(class);

    cdc->write = memory_char_driver_write;
}

static TypeInfo memory_char_driver_type_info = {
    .name = TYPE_MEMORY_CHAR_DRIVER,
    .parent = TYPE_CHAR_DRIVER,
    .instance_size = sizeof(MemoryCharDriver),
    .class_init = memory_char_driver_class_init,
    .instance_finalize = memory_char_driver_fini,
};

static void register_backends(void)
{
    type_register_static(&memory_char_driver_type_info);
}

device_init(register_backends);
