#include "qmp-output-visiter.h"
#include "qemu-queue.h"
#include "qemu-common.h"
#include "qemu-objects.h"

typedef struct QStackEntry
{
    QObject *value;
    QTAILQ_ENTRY(QStackEntry) node;
} QStackEntry;

typedef QTAILQ_HEAD(QStack, QStackEntry) QStack;

struct QmpOutputVisiter
{
    Visiter visiter;
    QStack stack;
};

#define qmp_output_add(qov, name, value) qmp_output_add_obj(qov, name, QOBJECT(value))
#define qmp_output_push(qov, value) qmp_output_push_obj(qov, QOBJECT(value))

static QmpOutputVisiter *to_qov(Visiter *v)
{
    return container_of(v, QmpOutputVisiter, visiter);
}

static void qmp_output_push_obj(QmpOutputVisiter *qov, QObject *value)
{
    QStackEntry *e = qemu_mallocz(sizeof(*e));

    e->value = value;
    QTAILQ_INSERT_HEAD(&qov->stack, e, node);
}

static QObject *qmp_output_pop(QmpOutputVisiter *qov)
{
    QStackEntry *e = QTAILQ_FIRST(&qov->stack);
    QObject *value;
    QTAILQ_REMOVE(&qov->stack, e, node);
    value = e->value;
    qemu_free(e);
    return value;
}

static QObject *qmp_output_first(QmpOutputVisiter *qov)
{
    QStackEntry *e = QTAILQ_LAST(&qov->stack, QStack);
    return e->value;
}

static QObject *qmp_output_last(QmpOutputVisiter *qov)
{
    QStackEntry *e = QTAILQ_FIRST(&qov->stack);
    return e->value;
}

static void qmp_output_add_obj(QmpOutputVisiter *qov, const char *name, QObject *value)
{
    QObject *cur;

    if (QTAILQ_EMPTY(&qov->stack)) {
        qmp_output_push_obj(qov, value);
        return;
    }

    cur = qmp_output_last(qov);

    switch (qobject_type(cur)) {
    case QTYPE_QDICT:
        qdict_put_obj(qobject_to_qdict(cur), name, value);
        break;
    case QTYPE_QLIST:
        qlist_append_obj(qobject_to_qlist(cur), value);
        break;
    default:
        qobject_decref(qmp_output_pop(qov));
        qmp_output_push_obj(qov, value);
        break;
    }
}

static void qmp_output_start_struct(Visiter *v, void **obj, const char *kind, const char *name, Error **errp)
{
    QmpOutputVisiter *qov = to_qov(v);
    QDict *dict = qdict_new();

    qmp_output_add(qov, name, dict);
    qmp_output_push(qov, dict);
}

static void qmp_output_end_struct(Visiter *v, Error **errp)
{
    QmpOutputVisiter *qov = to_qov(v);
    qmp_output_pop(qov);
}

static void qmp_output_start_list(Visiter *v, const char *name, Error **errp)
{
    QmpOutputVisiter *qov = to_qov(v);
    QList *list = qlist_new();

    qmp_output_add(qov, name, list);
    qmp_output_push(qov, list);
}

static GenericList *qmp_output_next_list(Visiter *v, GenericList **list, Error **errp)
{
    GenericList *retval = *list;
    *list = retval->next;
    return retval;
}

static void qmp_output_end_list(Visiter *v, Error **errp)
{
    QmpOutputVisiter *qov = to_qov(v);
    qmp_output_pop(qov);
}

static void qmp_output_type_int(Visiter *v, int64_t *obj, const char *name, Error **errp)
{
    QmpOutputVisiter *qov = to_qov(v);
    qmp_output_add(qov, name, qint_from_int(*obj));
}

static void qmp_output_type_bool(Visiter *v, bool *obj, const char *name, Error **errp)
{
    QmpOutputVisiter *qov = to_qov(v);
    qmp_output_add(qov, name, qbool_from_int(*obj));
}

static void qmp_output_type_str(Visiter *v, char **obj, const char *name, Error **errp)
{
    QmpOutputVisiter *qov = to_qov(v);
    qmp_output_add(qov, name, qstring_from_str(*obj));
}

static void qmp_output_type_number(Visiter *v, double *obj, const char *name, Error **errp)
{
    QmpOutputVisiter *qov = to_qov(v);
    qmp_output_add(qov, name, qfloat_from_double(*obj));
}

#if 0
static void qmp_output_type_enum(Visiter *v, int *obj, const char *kind, const char *name, Error **errp)
{
    QmpOutputVisiter *qov = to_qov(v);
    qmp_output_add(qov, name, qstring_from_str(qapi_enum_int2str(*obj)));
}
#else
static void qmp_output_type_enum(Visiter *v, int *obj, const char *kind, const char *name, Error **errp)
{
    int64_t value = *obj;
    qmp_output_type_int(v, &value, name, errp);
}
#endif

QObject *qmp_output_get_qobject(QmpOutputVisiter *qov)
{
    return qmp_output_first(qov);
}

Visiter *qmp_output_get_visiter(QmpOutputVisiter *v)
{
    return &v->visiter;
}

QmpOutputVisiter *qmp_output_visiter_new(void)
{
    QmpOutputVisiter *v;

    v = qemu_mallocz(sizeof(*v));

    v->visiter.start_struct = qmp_output_start_struct;
    v->visiter.end_struct = qmp_output_end_struct;
    v->visiter.start_list = qmp_output_start_list;
    v->visiter.next_list = qmp_output_next_list;
    v->visiter.end_list = qmp_output_end_list;
    v->visiter.type_enum = qmp_output_type_enum;
    v->visiter.type_int = qmp_output_type_int;
    v->visiter.type_bool = qmp_output_type_bool;
    v->visiter.type_str = qmp_output_type_str;
    v->visiter.type_number = qmp_output_type_number;

    QTAILQ_INIT(&v->stack);

    return v;
}
