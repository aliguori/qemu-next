#include "qmp-input-visiter.h"
#include "qemu-queue.h"
#include "qemu-common.h"
#include "qemu-objects.h"
#include "qerror.h"

#define QAPI_OBJECT_SIZE 512

#define QIV_STACK_SIZE 1024

typedef struct StackObject
{
    QObject *obj;
    QListEntry *entry;
} StackObject;

struct QmpInputVisiter
{
    Visiter visiter;
    QObject *obj;
    StackObject stack[QIV_STACK_SIZE];
    int nb_stack;
};

static QmpInputVisiter *to_qiv(Visiter *v)
{
    return container_of(v, QmpInputVisiter, visiter);
}

static QObject *qmp_input_get_object(QmpInputVisiter *qiv, const char *name)
{
    QObject *qobj;

    if (qiv->nb_stack == 0) {
        return qiv->obj;
    }

    qobj = qiv->stack[qiv->nb_stack - 1].obj;
    
    if (qobject_type(qobj) == QTYPE_QDICT) {
        return qdict_get(qobject_to_qdict(qobj), name);
    } else if (qobject_type(qobj) == QTYPE_QLIST) {
        return qlist_entry_obj(qiv->stack[qiv->nb_stack - 1].entry);
    }

    return NULL;
}

static void qmp_input_push(QmpInputVisiter *qiv, QObject *obj)
{
    qiv->stack[qiv->nb_stack].obj = obj;
    if (qobject_type(obj) == QTYPE_QLIST) {
        qiv->stack[qiv->nb_stack].entry = qlist_first(qobject_to_qlist(obj));
    }
    qiv->nb_stack++;

    assert(qiv->nb_stack < QIV_STACK_SIZE); // FIXME
}

static void qmp_input_pop(QmpInputVisiter *qiv)
{
    qiv->nb_stack--;
    assert(qiv->nb_stack >= 0); // FIXME
}

static void qmp_input_start_struct(Visiter *v, void **obj, const char *kind, const char *name, Error **errp)
{
    QmpInputVisiter *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QDICT) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name, "object");
        return;
    }

    qmp_input_push(qiv, qobj);

    *obj = qemu_mallocz(QAPI_OBJECT_SIZE);
}

static void qmp_input_end_struct(Visiter *v, Error **errp)
{
    QmpInputVisiter *qiv = to_qiv(v);

    qmp_input_pop(qiv);
}

static void qmp_input_start_list(Visiter *v, const char *name, Error **errp)
{
    QmpInputVisiter *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QLIST) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name, "list");
        return;
    }

    qmp_input_push(qiv, qobj);
}

static GenericList *qmp_input_next_list(Visiter *v, GenericList **list, Error **errp)
{
    QmpInputVisiter *qiv = to_qiv(v);
    GenericList *entry;
    StackObject *so = &qiv->stack[qiv->nb_stack - 1];

    if (so->entry == NULL) {
        return NULL;
    }

    entry = qemu_mallocz(sizeof(*entry));
    if (*list) {
        so->entry = qlist_next(so->entry);
        if (so->entry == NULL) {
            qemu_free(entry);
            return NULL;
        }
        (*list)->next = entry;
    }
    *list = entry;

    
    return entry;
}

static void qmp_input_end_list(Visiter *v, Error **errp)
{
    QmpInputVisiter *qiv = to_qiv(v);

    qmp_input_pop(qiv);
}

static void qmp_input_type_int(Visiter *v, int64_t *obj, const char *name, Error **errp)
{
    QmpInputVisiter *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QINT) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name, "integer");
        return;
    }

    *obj = qint_get_int(qobject_to_qint(qobj));
}

static void qmp_input_type_bool(Visiter *v, bool *obj, const char *name, Error **errp)
{
    QmpInputVisiter *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QBOOL) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name, "boolean");
        return;
    }

    *obj = qbool_get_int(qobject_to_qbool(qobj));
}

static void qmp_input_type_str(Visiter *v, char **obj, const char *name, Error **errp)
{
    QmpInputVisiter *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QSTRING) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name, "string");
        return;
    }

    *obj = qemu_strdup(qstring_get_str(qobject_to_qstring(qobj)));
}

static void qmp_input_type_number(Visiter *v, double *obj, const char *name, Error **errp)
{
    QmpInputVisiter *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QFLOAT) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name, "double");
        return;
    }

    *obj = qfloat_get_double(qobject_to_qfloat(qobj));
}

static void qmp_input_type_enum(Visiter *v, int *obj, const char *kind, const char *name, Error **errp)
{
    int64_t value;
    qmp_input_type_int(v, &value, name, errp);
    *obj = value;
}

Visiter *qmp_input_get_visiter(QmpInputVisiter *v)
{
    return &v->visiter;
}

QmpInputVisiter *qmp_input_visiter_new(QObject *obj)
{
    QmpInputVisiter *v;

    v = qemu_mallocz(sizeof(*v));

    v->visiter.start_struct = qmp_input_start_struct;
    v->visiter.end_struct = qmp_input_end_struct;
    v->visiter.start_list = qmp_input_start_list;
    v->visiter.next_list = qmp_input_next_list;
    v->visiter.end_list = qmp_input_end_list;
    v->visiter.type_enum = qmp_input_type_enum;
    v->visiter.type_int = qmp_input_type_int;
    v->visiter.type_bool = qmp_input_type_bool;
    v->visiter.type_str = qmp_input_type_str;
    v->visiter.type_number = qmp_input_type_number;

    v->obj = obj;

    return v;
}
