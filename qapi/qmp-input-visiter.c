#include "qmp-input-visiter.h"
#include "qemu-queue.h"
#include "qemu-common.h"
#include "qemu-objects.h"

typedef struct QStackEntry
{
    QObject *value;
    QTAILQ_ENTRY(QStackEntry) node;
} QStackEntry;

typedef QTAILQ_HEAD(QStack, QStackEntry) QStack;

struct QmpOuputVisiter
{
    Visiter visiter;
    QStack stack;
};

#define qmp_input_add(qov, name, value) qmp_input_add_obj(qov, name, QOBJECT(value))
#define qmp_input_push(qov, value) qmp_input_push_obj(qov, QOBJECT(value))

static QmpInputVisiter *to_qov(Visiter *v)
{
    return container_of(v, QmpInputVisiter, visiter);
}

static void qmp_input_push_obj(QmpInputVisiter *qov, QObject *value)
{
    QStackEntry *e = qemu_mallocz(sizeof(*e));

    e->value = value;
    QTAILQ_INSERT_HEAD(&qov->stack, e, node);
}

static QObject *qmp_input_pop(QmpInputVisiter *qov)
{
    QStackEntry *e = QTAILQ_FIRST(&qov->stack);
    QObject *value;
    QTAILQ_REMOVE(&qov->stack, e, node);
    value = e->value;
    qemu_free(e);
    return value;
}

static QObject *qmp_input_first(QmpInputVisiter *qov)
{
    QStackEntry *e = QTAILQ_LAST(&qov->stack, QStack);
    return e->value;
}

static QObject *qmp_input_last(QmpInputVisiter *qov)
{
    QStackEntry *e = QTAILQ_FIRST(&qov->stack);
    return e->value;
}

static void qmp_input_add_obj(QmpInputVisiter *qov, const char *name, QObject *value)
{
    QObject *cur;

    if (QTAILQ_EMPTY(&qov->stack)) {
        qmp_input_push_obj(qov, value);
        return;
    }

    cur = qmp_input_last(qov);

    switch (qobject_type(cur)) {
    case QTYPE_QDICT:
        qdict_put_obj(qobject_to_qdict(cur), name, value);
        break;
    case QTYPE_QLIST:
        qlist_append_obj(qobject_to_qlist(cur), value);
        break;
    default:
        qobject_decref(qmp_input_pop(qov));
        qmp_input_push_obj(qov, value);
        break;
    }
}

static void qmp_input_start_struct(Visiter *v, void **obj, const char *kind, const char *name, Error **errp)
{
    QmpInputVisiter *qov = to_qov(v);
    QDict *dict = qdict_new();

    qmp_input_add(qov, name, dict);
    qmp_input_push(qov, dict);
}

static void qmp_input_end_struct(Visiter *v, Error **errp)
{
    QmpInputVisiter *qov = to_qov(v);
    qmp_input_pop(qov);
}

static void qmp_input_start_list(Visiter *v, const char *name, Error **errp)
{
    QmpInputVisiter *qov = to_qov(v);
    QList *list = qlist_new();

    qmp_input_add(qov, name, list);
    qmp_input_push(qov, list);
}

static GenericList *qmp_input_next_list(Visiter *v, GenericList **list, Error **errp)
{
    GenericList *retval = *list;
    *list = retval->next;
    return retval;
}

static void qmp_input_end_list(Visiter *v, Error **errp)
{
    QmpInputVisiter *qov = to_qov(v);
    qmp_input_pop(qov);
}

static void qmp_input_type_int(Visiter *v, int64_t *obj, const char *name, Error **errp)
{
    QmpInputVisiter *qov = to_qov(v);
    qmp_input_add(qov, name, qint_from_int(*obj));
}

static void qmp_input_type_bool(Visiter *v, bool *obj, const char *name, Error **errp)
{
    QmpInputVisiter *qov = to_qov(v);
    qmp_input_add(qov, name, qbool_from_int(*obj));
}

static void qmp_input_type_str(Visiter *v, char **obj, const char *name, Error **errp)
{
    QmpInputVisiter *qov = to_qov(v);
    qmp_input_add(qov, name, qstring_from_str(*obj));
}

static void qmp_input_type_number(Visiter *v, double *obj, const char *name, Error **errp)
{
    QmpInputVisiter *qov = to_qov(v);
    // if last == NULL
    // return missing option
    // if name == NULL:
    //    if type(last) == list:
    //       find next item
    // else:
    //    if type(last) == dict:
    //       find key, otherwise return error
    qmp_input_add(qov, name, qfloat_from_double(*obj));
}

#if 0
static void qmp_input_type_enum(Visiter *v, int *obj, const char *kind, const char *name, Error **errp)
{
    QmpInputVisiter *qov = to_qov(v);
    qmp_input_add(qov, name, qstring_from_str(qapi_enum_int2str(*obj)));
}
#else
static void qmp_input_type_enum(Visiter *v, int *obj, const char *kind, const char *name, Error **errp)
{
    int64_t value = *obj;
    qmp_input_type_int(v, &value, name, errp);
}
#endif

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

    QTAILQ_INIT(&v->stack);
    qmp_input_push_obj(obj);

    return v;
}
