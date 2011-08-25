/*
 * QEMUFile Output Visitor
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Michael Roth   <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu-file-input-visitor.h"
#include "qemu-queue.h"
#include "qemu-common.h"
#include "qemu-objects.h"
#include "hw/hw.h"
#include "qerror.h"

typedef struct {
    size_t elem_count;
    size_t elem_size;
    size_t pos;
} ArrayInfo;

typedef struct StackEntry
{
    enum {
        QFIV_ARRAY,
        QFIV_LIST,
        QFIV_STRUCT,
    } type;
    ArrayInfo array_info;
    QTAILQ_ENTRY(StackEntry) node;
} StackEntry;

struct QemuFileInputVisitor
{
    Visitor visitor;
    QTAILQ_HEAD(, StackEntry) stack;
    QEMUFile *file;
};

static QemuFileInputVisitor *to_iv(Visitor *v)
{
    return container_of(v, QemuFileInputVisitor, visitor);
}

static void qemu_file_input_push(QemuFileInputVisitor *iv, StackEntry *e)
{
    QTAILQ_INSERT_HEAD(&iv->stack, e, node);
}

static void qemu_file_input_push_array(QemuFileInputVisitor *iv, ArrayInfo ai)
{
    StackEntry *e = g_malloc0(sizeof(*e));
    e->type = QFIV_ARRAY;
    e->array_info = ai;
    qemu_file_input_push(iv, e);
}

static void qemu_file_input_push_list(QemuFileInputVisitor *iv)
{
    StackEntry *e = g_malloc0(sizeof(*e));
    e->type = QFIV_LIST;
    qemu_file_input_push(iv, e);
}

static void qemu_file_input_push_struct(QemuFileInputVisitor *iv)
{
    StackEntry *e = g_malloc0(sizeof(*e));
    e->type = QFIV_STRUCT;
    qemu_file_input_push(iv, e);
}

static void *qemu_file_input_pop(QemuFileInputVisitor *iv)
{
    StackEntry *e = QTAILQ_FIRST(&iv->stack);
    QTAILQ_REMOVE(&iv->stack, e, node);
    return e;
}

static bool qemu_file_input_is_array(QemuFileInputVisitor *iv)
{
    StackEntry *e = QTAILQ_FIRST(&iv->stack);
    return e->type == QFIV_ARRAY;
}

static bool qemu_file_input_is_list(QemuFileInputVisitor *ov)
{
    StackEntry *e = QTAILQ_FIRST(&ov->stack);
    return e && e->type == QFIV_LIST;
}

static void qemu_file_input_start_struct(Visitor *v, void **obj,
                                         const char *kind,
                                         const char *name, size_t size,
                                         Error **errp)
{
    QemuFileInputVisitor *iv = to_iv(v);

    if (obj && *obj == NULL) {
        *obj = g_malloc0(size);
    }
    qemu_file_input_push_struct(iv);
}

static void qemu_file_input_end_struct(Visitor *v, Error **errp)
{
    QemuFileInputVisitor *iv = to_iv(v);
    StackEntry *e = qemu_file_input_pop(iv);

    if (!e || e->type != QFIV_STRUCT) {
        error_set(errp, QERR_UNDEFINED_ERROR);
        return;
    }
    g_free(e);
}

static void qemu_file_input_start_list(Visitor *v, const char *name,
                                       Error **errp)
{
    QemuFileInputVisitor *iv = to_iv(v);
    qemu_file_input_push_list(iv);
}

static GenericList *qemu_file_input_next_list(Visitor *v, GenericList **list,
                                           Error **errp)
{
    QemuFileInputVisitor *iv = to_iv(v);
    GenericList *entry;

    if (!qemu_file_input_is_list(iv)) {
        error_set(errp, QERR_UNDEFINED_ERROR);
    }

    entry = g_malloc0(sizeof(*entry));
    if (*list) {
        (*list)->next = entry;
    }

    *list = entry;
    return entry;
}

static void qemu_file_input_end_list(Visitor *v, Error **errp)
{
    QemuFileInputVisitor *iv = to_iv(v);
    StackEntry *e = qemu_file_input_pop(iv);
    if (!e || e->type != QFIV_LIST) {
        error_set(errp, QERR_UNDEFINED_ERROR);
        return;
    }
    g_free(e);
}

static void qemu_file_input_start_array(Visitor *v, void **obj,
                                        const char *name,
                                        size_t elem_count,
                                        size_t elem_size,
                                        Error **errp)
{
    QemuFileInputVisitor *iv = to_iv(v);
    ArrayInfo ai = {
        .elem_count = elem_count,
        .elem_size = elem_size,
        .pos = 0
    };
    if (obj && *obj == NULL) {
        *obj = g_malloc0(elem_count * elem_size);
    }
    qemu_file_input_push_array(iv, ai);
}

static void qemu_file_input_next_array(Visitor *v, Error **errp)
{
    QemuFileInputVisitor *iv = to_iv(v);
    StackEntry *e = QTAILQ_FIRST(&iv->stack);

    if (!qemu_file_input_is_array(iv) ||
        e->array_info.pos >= e->array_info.elem_count) {
        error_set(errp, QERR_UNDEFINED_ERROR);
    }

    e->array_info.pos++;
}

static void qemu_file_input_end_array(Visitor *v, Error **errp)
{
    QemuFileInputVisitor *iv = to_iv(v);
    StackEntry *e = qemu_file_input_pop(iv);
    if (!e || e->type != QFIV_ARRAY) {
        error_set(errp, QERR_UNDEFINED_ERROR);
        return;
    }
    g_free(e);
}

static void qemu_file_input_type_str(Visitor *v, char **obj, const char *name,
                                  Error **errp)
{
    if (obj) {
        g_free(*obj);
    }
}

static void qemu_file_input_type_uint8(Visitor *v, uint8_t *obj,
                                         const char *name, Error **errp)
{
    QemuFileInputVisitor *ov = container_of(v, QemuFileInputVisitor, visitor);
    *obj = qemu_get_byte(ov->file);
}

static void qemu_file_input_type_uint16(Visitor *v, uint16_t *obj,
                                          const char *name, Error **errp)
{
    QemuFileInputVisitor *ov = container_of(v, QemuFileInputVisitor, visitor);
    *obj = qemu_get_be16(ov->file);
}

static void qemu_file_input_type_uint32(Visitor *v, uint32_t *obj,
                                          const char *name, Error **errp)
{
    QemuFileInputVisitor *ov = container_of(v, QemuFileInputVisitor, visitor);
    *obj = qemu_get_be32(ov->file);
}

static void qemu_file_input_type_uint64(Visitor *v, uint64_t *obj,
                                          const char *name, Error **errp)
{
    QemuFileInputVisitor *ov = container_of(v, QemuFileInputVisitor, visitor);
    *obj = qemu_get_be64(ov->file);
}

static void qemu_file_input_type_int8(Visitor *v, int8_t *obj,
                                        const char *name, Error **errp)
{
    QemuFileInputVisitor *ov = container_of(v, QemuFileInputVisitor, visitor);
    *obj = qemu_get_sbyte(ov->file);
}

static void qemu_file_input_type_int16(Visitor *v, int16_t *obj,
                                         const char *name, Error **errp)
{
    QemuFileInputVisitor *ov = container_of(v, QemuFileInputVisitor, visitor);
    *obj = qemu_get_sbe16(ov->file);
}

static void qemu_file_input_type_int32(Visitor *v, int32_t *obj,
                                         const char *name, Error **errp)
{
    QemuFileInputVisitor *ov = container_of(v, QemuFileInputVisitor, visitor);
    *obj = qemu_get_sbe32(ov->file);
}

static void qemu_file_input_type_int64(Visitor *v, int64_t *obj,
                                         const char *name, Error **errp)
{
    QemuFileInputVisitor *ov = container_of(v, QemuFileInputVisitor, visitor);
    *obj = qemu_get_sbe64(ov->file);
}

static void qemu_file_input_type_bool(Visitor *v, bool *obj, const char *name,
                                     Error **errp)
{
    uint8_t val;
    qemu_file_input_type_uint8(v, &val, name, errp);
    *obj = val;
}

static void qemu_file_input_type_number(Visitor *v, double *obj,
                                        const char *name, Error **errp)
{
    uint64_t *val = (uint64_t *)obj;
    qemu_file_input_type_uint64(v, val, name, errp);
}

static void qemu_file_input_type_enum(Visitor *v, int *obj,
                                      const char *strings[], const char *kind,
                                      const char *name, Error **errp)
{
}

Visitor *qemu_file_input_get_visitor(QemuFileInputVisitor *ov)
{
    return &ov->visitor;
}

void qemu_file_input_visitor_cleanup(QemuFileInputVisitor *ov)
{
    g_free(ov);
}

QemuFileInputVisitor *qemu_file_input_visitor_new(QEMUFile *f)
{
    QemuFileInputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->file = f;

    v->visitor.start_struct = qemu_file_input_start_struct;
    v->visitor.end_struct = qemu_file_input_end_struct;
    v->visitor.start_list = qemu_file_input_start_list;
    v->visitor.next_list = qemu_file_input_next_list;
    v->visitor.end_list = qemu_file_input_end_list;
    v->visitor.start_array = qemu_file_input_start_array;
    v->visitor.next_array = qemu_file_input_next_array;
    v->visitor.end_array = qemu_file_input_end_array;
    v->visitor.type_enum = qemu_file_input_type_enum;
    v->visitor.type_int = qemu_file_input_type_int64;
    v->visitor.type_uint8 = qemu_file_input_type_uint8;
    v->visitor.type_uint16 = qemu_file_input_type_uint16;
    v->visitor.type_uint32 = qemu_file_input_type_uint32;
    v->visitor.type_uint64 = qemu_file_input_type_uint64;
    v->visitor.type_int8 = qemu_file_input_type_int8;
    v->visitor.type_int16 = qemu_file_input_type_int16;
    v->visitor.type_int32 = qemu_file_input_type_int32;
    v->visitor.type_int64 = qemu_file_input_type_int64;
    v->visitor.type_bool = qemu_file_input_type_bool;
    v->visitor.type_str = qemu_file_input_type_str;
    v->visitor.type_number = qemu_file_input_type_number;

    QTAILQ_INIT(&v->stack);

    return v;
}
