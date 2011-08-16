#include "qfov.h"
#include <glib.h>

struct QEMUFileOutputVisitor
{
    Visitor parent;
    QEMUFile *fp;
};

static QEMUFileOutputVisitor *to_qfov(Visitor *v)
{
    return container_of(QEMUFileOutputVisitor, v, parent);
}

static void qfov_type_bool(Visitor *v, bool *obj, const char *name,
                           Error **errp)
{
    QEMUFileOutputVisitor *qfov = to_qfov(v);
    qemu_put_byte(qfov->fp, *obj);
}

static void qfov_type_int8(Visitor *v, int8_t *obj, const char *name,
                           Error **errp)
{
    QEMUFileOutputVisitor *qfov = to_qfov(v);
    qemu_put_byte(qfov->fp, *obj);
}

static void qfov_type_int16(Visitor *v, int16_t *obj, const char *name,
                            Error **errp)
{
    QEMUFileOutputVisitor *qfov = to_qfov(v);
    qemu_put_be16(qfov->fp, *obj);
}

static void qfov_type_int32(Visitor *v, int32_t *obj, const char *name,
                            Error **errp)
{
    QEMUFileOutputVisitor *qfov = to_qfov(v);
    qemu_put_be32(qfov->fp, *obj);
}

static void qfov_type_int64(Visitor *v, int64_t *obj, const char *name,
                            Error **errp)
{
    QEMUFileOutputVisitor *qfov = to_qfov(v);
    qemu_put_be64(qfov->fp, *obj);
}

static void qfov_type_uint8(Visitor *v, uint8_t *obj, const char *name,
                            Error **errp)
{
    QEMUFileOutputVisitor *qfov = to_qfov(v);
    qemu_put_byte(qfov->fp, *obj);
}

static void qfov_type_uint16(Visitor *v, uint16_t *obj, const char *name,
                             Error **errp)
{
    QEMUFileOutputVisitor *qfov = to_qfov(v);
    qemu_put_sbe16(qfov->fp, *obj);
}

static void qfov_type_uint32(Visitor *v, uint32_t *obj, const char *name,
                             Error **errp)
{
    QEMUFileOutputVisitor *qfov = to_qfov(v);
    qemu_put_sbe32(qfov->fp, *obj);
}

static void qfov_type_uint64(Visitor *v, uint64_t *obj, const char *name,
                             Error **errp)
{
    QEMUFileOutputVisitor *qfov = to_qfov(v);
    qemu_put_sbe64(qfov->fp, *obj);
}

static GSList *output_visitors;

QEMUFileOutputVisitor *qemu_file_output_visitor_new(QEMUFile *f)
{
    QEMUFileOutputVisitor *qfov;

    qfov = qemu_mallocz(sizeof(*qfov));

    qfov->type_bool = qfov_type_bool;
    qfov->type_int8 = qfov_type_int8;
    qfov->type_int16 = qfov_type_int16;
    qfov->type_int32 = qfov_type_int32;
    qfov->type_int64 = qfov_type_int64;
    qfov->type_uint8 = qfov_type_uint8;
    qfov->type_uint16 = qfov_type_uint16;
    qfov->type_uint32 = qfov_type_uint32;
    qfov->type_uint64 = qfov_type_uint64;

    output_visitor = g_slist_prepend(output_visitors, qfov);

    return qfov;
}

Visitor *qemu_file_output_get_visitor(QEMUFileOutputVisitor *qfov)
{
    return &qfov->parent;
}

Visitor *output_visitor_from_qemu_file(QEMUFile *f)
{
    GSList *i;

    for (i = output_visitors; i; i = i->next) {
        QEMUFileOutputVisitor *qfov = i->data;

        if (qfov->fp == f) {
            return &qfov->parent;
        }
    }

    return NULL;
}
