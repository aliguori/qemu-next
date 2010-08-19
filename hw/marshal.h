#ifndef QEMU_MARSHAL_H
#define QEMU_MARSHAL_H

#include "qemu-common.h"

typedef struct Marshaller Marshaller;
typedef struct MarshallerOperations MarshallerOperations;

#include "builtin-marshal.h"

struct MarshallerOperations
{
    void (*m_uint8)(Marshaller *m, uint8_t *v, const char *name);
    void (*m_uint16)(Marshaller *m, uint16_t *v, const char *name);
    void (*m_uint32)(Marshaller *m, uint32_t *v, const char *name);
    void (*m_uint64)(Marshaller *m, uint64_t *v, const char *name);

    void (*m_int8)(Marshaller *m, int8_t *v, const char *name);
    void (*m_int16)(Marshaller *m, int16_t *v, const char *name);
    void (*m_int32)(Marshaller *m, int32_t *v, const char *name);
    void (*m_int64)(Marshaller *m, int64_t *v, const char *name);

    void (*m_start_struct)(Marshaller *m, const char *kind, const char *name);
    void (*m_end_struct)(Marshaller *m);

    void (*m_start_array)(Marshaller *m, const char *name);
    void (*m_end_array)(Marshaller *m);
};

struct Marshaller
{
    MarshallerOperations *ops;
};

static inline void marshal_uint8(Marshaller *m, uint8_t *v, const char *name)
{
    m->ops->m_uint8(m, v, name);
}

static inline void marshal_uint16(Marshaller *m, uint16_t *v, const char *name)
{
    m->ops->m_uint16(m, v, name);
}

static inline void marshal_uint32(Marshaller *m, uint32_t *v, const char *name)
{
    m->ops->m_uint32(m, v, name);
}

static inline void marshal_uint64(Marshaller *m, uint64_t *v, const char *name)
{
    m->ops->m_uint64(m, v, name);
}

static inline void marshal_int8(Marshaller *m, int8_t *v, const char *name)
{
    m->ops->m_int8(m, v, name);
}

static inline void marshal_int16(Marshaller *m, int16_t *v, const char *name)
{
    m->ops->m_int16(m, v, name);
}

static inline void marshal_int32(Marshaller *m, int32_t *v, const char *name)
{
    m->ops->m_int32(m, v, name);
}

static inline void marshal_int64(Marshaller *m, int64_t *v, const char *name)
{
    m->ops->m_int64(m, v, name);
}

static inline void marshal_start_struct(Marshaller *m, const char *kind, const char *name)
{
    m->ops->m_start_struct(m, kind, name);
}

static inline void marshal_end_struct(Marshaller *m)
{
    m->ops->m_end_struct(m);
}

static inline void marshal_start_array(Marshaller *m, const char *name)
{
    m->ops->m_start_array(m, name);
}

static inline void marshal_end_array(Marshaller *m)
{
    m->ops->m_end_array(m);
}

#endif
