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

#include "qemu-file-output-visitor.h"
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
        QFOV_ARRAY,
        QFOV_LIST,
        QFOV_STRUCT,
    } type;
    ArrayInfo array_info;
    bool is_list_head;
    QTAILQ_ENTRY(StackEntry) node;
} StackEntry;

struct QemuFileOutputVisitor
{
    Visitor visitor;
    QTAILQ_HEAD(, StackEntry) stack;
    QEMUFile *file;
};

static QemuFileOutputVisitor *to_ov(Visitor *v)
{
    return container_of(v, QemuFileOutputVisitor, visitor);
}

static void qemu_file_output_push(QemuFileOutputVisitor *ov, StackEntry *e)
{
    QTAILQ_INSERT_HEAD(&ov->stack, e, node);
}

static void qemu_file_output_push_array(QemuFileOutputVisitor *ov, ArrayInfo ai)
{
    StackEntry *e = g_malloc0(sizeof(*e));
    e->type = QFOV_ARRAY;
    e->array_info = ai;
    qemu_file_output_push(ov, e);
}

static void qemu_file_output_push_list(QemuFileOutputVisitor *ov)
{
    StackEntry *e = g_malloc0(sizeof(*e));
    e->type = QFOV_LIST;
    e->is_list_head = true;
    qemu_file_output_push(ov, e);
}

static void qemu_file_output_push_struct(QemuFileOutputVisitor *ov)
{
    StackEntry *e = g_malloc0(sizeof(*e));
    e->type = QFOV_STRUCT;
    qemu_file_output_push(ov, e);
}

static StackEntry *qemu_file_output_pop(QemuFileOutputVisitor *ov)
{
    StackEntry *e = QTAILQ_FIRST(&ov->stack);
    QTAILQ_REMOVE(&ov->stack, e, node);
    return e;
}

static bool qemu_file_output_is_array(QemuFileOutputVisitor *ov)
{
    StackEntry *e = QTAILQ_FIRST(&ov->stack);
    return e && e->type == QFOV_ARRAY;
}

static bool qemu_file_output_is_list(QemuFileOutputVisitor *ov)
{
    StackEntry *e = QTAILQ_FIRST(&ov->stack);
    return e && e->type == QFOV_LIST;
}

static void qemu_file_output_start_struct(Visitor *v, void **obj,
                                          const char *kind, const char *name,
                                          size_t unused, Error **errp)
{
    QemuFileOutputVisitor *ov = to_ov(v);

    qemu_file_output_push_struct(ov);
}

static void qemu_file_output_end_struct(Visitor *v, Error **errp)
{
    QemuFileOutputVisitor *ov = to_ov(v);
    StackEntry *e = qemu_file_output_pop(ov);

    if (!e || e->type != QFOV_STRUCT) {
        error_set(errp, QERR_UNDEFINED_ERROR);
        return;
    }
    g_free(e);
}

static void qemu_file_output_start_list(Visitor *v, const char *name,
                                        Error **errp)
{
    QemuFileOutputVisitor *ov = to_ov(v);
    qemu_file_output_push_list(ov);
}

static GenericList *qemu_file_output_next_list(Visitor *v, GenericList **list,
                                           Error **errp)
{
    QemuFileOutputVisitor *ov = to_ov(v);
    GenericList *entry = *list;
    StackEntry *e = QTAILQ_FIRST(&ov->stack);

    if (!entry || !qemu_file_output_is_list(ov)) {
        error_set(errp, QERR_UNDEFINED_ERROR);
    }

    /* The way the list iterator is currently used unfortunately clobbers
     * **list by subseqently assigning our return value to the same container.
     * This can cause an infinite loop, but we can get around this by tracking
     * a bit of state to note when we should pass back the next entry rather
     * than the current one.
     */
    if (e->is_list_head) {
        e->is_list_head = false;
        return entry;
    }

    *list = entry->next;
    return entry->next;
}

static void qemu_file_output_end_list(Visitor *v, Error **errp)
{
    QemuFileOutputVisitor *ov = to_ov(v);
    StackEntry *e = qemu_file_output_pop(ov);
    if (!e || e->type != QFOV_LIST) {
        error_set(errp, QERR_UNDEFINED_ERROR);
    }
    g_free(e);
}

static void qemu_file_output_start_array(Visitor *v, void **obj,
                                         const char *name,
                                         size_t elem_count,
                                         size_t elem_size, Error **errp)
{
    QemuFileOutputVisitor *ov = to_ov(v);
    ArrayInfo ai = {
        .elem_count = elem_count,
        .elem_size = elem_size,
        .pos = 0
    };
    qemu_file_output_push_array(ov, ai);
}

static void qemu_file_output_next_array(Visitor *v, Error **errp)
{
    QemuFileOutputVisitor *ov = to_ov(v);
    StackEntry *e = QTAILQ_FIRST(&ov->stack);
    if (!qemu_file_output_is_array(ov) ||
        e->array_info.pos >= e->array_info.elem_count) {
        error_set(errp, QERR_UNDEFINED_ERROR);
    }

    e->array_info.pos++;
}

static void qemu_file_output_end_array(Visitor *v, Error **errp)
{
    QemuFileOutputVisitor *ov = to_ov(v);
    StackEntry *e = qemu_file_output_pop(ov);
    if (!e || e->type != QFOV_ARRAY) {
        error_set(errp, QERR_UNDEFINED_ERROR);
        return;
    }
    g_free(e);
}

static void qemu_file_output_type_str(Visitor *v, char **obj, const char *name,
                                      Error **errp)
{
    if (obj) {
        g_free(*obj);
    }
}

static void qemu_file_output_type_uint8(Visitor *v, uint8_t *obj,
                                          const char *name,
                                          Error **errp)
{
    QemuFileOutputVisitor *ov = container_of(v, QemuFileOutputVisitor, visitor);
    qemu_put_byte(ov->file, *obj);
}

static void qemu_file_output_type_uint16(Visitor *v, uint16_t *obj,
                                           const char *name, Error **errp)
{
    QemuFileOutputVisitor *ov = container_of(v, QemuFileOutputVisitor, visitor);
    qemu_put_be16(ov->file, *obj);
}

static void qemu_file_output_type_uint32(Visitor *v, uint32_t *obj,
                                           const char *name, Error **errp)
{
    QemuFileOutputVisitor *ov = container_of(v, QemuFileOutputVisitor, visitor);
    qemu_put_be32(ov->file, *obj);
}

static void qemu_file_output_type_uint64(Visitor *v, uint64_t *obj,
                                           const char *name, Error **errp)
{
    QemuFileOutputVisitor *ov = container_of(v, QemuFileOutputVisitor, visitor);
    qemu_put_be64(ov->file, *obj);
}

static void qemu_file_output_type_int8(Visitor *v, int8_t *obj,
                                         const char *name, Error **errp)
{
    QemuFileOutputVisitor *ov = container_of(v, QemuFileOutputVisitor, visitor);
    qemu_put_sbyte(ov->file, *obj);
}

static void qemu_file_output_type_int16(Visitor *v, int16_t *obj,
                                          const char *name, Error **errp)
{
    QemuFileOutputVisitor *ov = container_of(v, QemuFileOutputVisitor, visitor);
    qemu_put_sbe16(ov->file, *obj);
}

static void qemu_file_output_type_int32(Visitor *v, int32_t *obj,
                                          const char *name, Error **errp)
{
    QemuFileOutputVisitor *ov = container_of(v, QemuFileOutputVisitor, visitor);
    qemu_put_sbe32(ov->file, *obj);
}

static void qemu_file_output_type_int64(Visitor *v, int64_t *obj,
                                          const char *name, Error **errp)
{
    QemuFileOutputVisitor *ov = container_of(v, QemuFileOutputVisitor, visitor);
    qemu_put_be64(ov->file, *obj);
}

static void qemu_file_output_type_bool(Visitor *v, bool *obj, const char *name,
                                   Error **errp)
{
    uint8_t val = *obj;
    qemu_file_output_type_uint8(v, &val, name, errp);
}

static void qemu_file_output_type_number(Visitor *v, double *obj,
                                         const char *name, Error **errp)
{
    uint64_t *val = (uint64_t *)obj;
    qemu_file_output_type_uint64(v, val, name, errp);
}

static void qemu_file_output_type_enum(Visitor *v, int *obj,
                                       const char *strings[],
                                       const char *kind, const char *name,
                                       Error **errp)
{
}

Visitor *qemu_file_output_get_visitor(QemuFileOutputVisitor *v)
{
    return &v->visitor;
}

void qemu_file_output_visitor_cleanup(QemuFileOutputVisitor *ov)
{
    g_free(ov);
}

QemuFileOutputVisitor *qemu_file_output_visitor_new(QEMUFile *f)
{
    QemuFileOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->file = f;

    v->visitor.start_struct = qemu_file_output_start_struct;
    v->visitor.end_struct = qemu_file_output_end_struct;
    v->visitor.start_list = qemu_file_output_start_list;
    v->visitor.next_list = qemu_file_output_next_list;
    v->visitor.end_list = qemu_file_output_end_list;
    v->visitor.start_array = qemu_file_output_start_array;
    v->visitor.next_array = qemu_file_output_next_array;
    v->visitor.end_array = qemu_file_output_end_array;
    v->visitor.type_enum = qemu_file_output_type_enum;
    v->visitor.type_int = qemu_file_output_type_int64;
    v->visitor.type_uint8 = qemu_file_output_type_uint8;
    v->visitor.type_uint16 = qemu_file_output_type_uint16;
    v->visitor.type_uint32 = qemu_file_output_type_uint32;
    v->visitor.type_uint64 = qemu_file_output_type_uint64;
    v->visitor.type_int8 = qemu_file_output_type_int8;
    v->visitor.type_int16 = qemu_file_output_type_int16;
    v->visitor.type_int32 = qemu_file_output_type_int32;
    v->visitor.type_int64 = qemu_file_output_type_int64;
    v->visitor.type_bool = qemu_file_output_type_bool;
    v->visitor.type_str = qemu_file_output_type_str;
    v->visitor.type_number = qemu_file_output_type_number;

    QTAILQ_INIT(&v->stack);

    return v;
}
