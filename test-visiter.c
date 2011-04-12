#include <glib.h>
#include "qapi/qmp-output-visiter.h"
#include "qapi/qmp-input-visiter.h"
#include "qemu-objects.h"

typedef struct TestStruct
{
    int64_t x;
    int64_t y;
} TestStruct;

typedef struct TestStructList
{
    TestStruct *value;
    struct TestStructList *next;
} TestStructList;

static void visit_type_TestStruct(Visiter *v, TestStruct **obj, const char *name, Error **errp)
{
    visit_start_struct(v, (void **)obj, "TestStruct", name, errp);
    visit_type_int(v, &(*obj)->x, "x", errp);
    visit_type_int(v, &(*obj)->y, "y", errp);
    visit_end_struct(v, errp);
}

static void visit_type_TestStructList(Visiter *m, TestStructList ** obj, const char *name, Error **errp)
{
    GenericList *i;

    visit_start_list(m, name, errp);
    
    for (i = visit_next_list(m, (GenericList **)obj, errp); i; i = visit_next_list(m, &i, errp)) {
        TestStructList *native_i = (TestStructList *)i;
        visit_type_TestStruct(m, &native_i->value, NULL, errp);
    }

    visit_end_list(m, errp);
}

int main(int argc, char **argv)
{
    QmpOutputVisiter *mo;
    QmpInputVisiter *mi;
    Visiter *v;
    TestStruct ts = { 42, 82 };
    TestStruct *pts = &ts;
    TestStructList *lts = NULL;
    Error *err = NULL;
    QObject *obj;
    QString *str;

    g_test_init(&argc, &argv, NULL);

    mo = qmp_output_visiter_new();
    v = qmp_output_get_visiter(mo);

    visit_type_TestStruct(v, &pts, "ts", &err);

    obj = qmp_output_get_qobject(mo);

    str = qobject_to_json(obj);

    printf("%s\n", qstring_get_str(str));

    QDECREF(str);

    obj = QOBJECT(qint_from_int(0x42));

    mi = qmp_input_visiter_new(obj);
    v = qmp_input_get_visiter(mi);

    int64_t value = 0;

    visit_type_int(v, &value, "value", &err);
    if (err) {
        printf("%s\n", error_get_pretty(err));
        return 1;
    }

    g_assert(value == 0x42);

    qobject_decref(obj);

    obj = qobject_from_json("{'x': 42, 'y': 84}");
    mi = qmp_input_visiter_new(obj);
    v = qmp_input_get_visiter(mi);

    pts = NULL;

    visit_type_TestStruct(v, &pts, "ts", &err);
    if (err) {
        printf("%s\n", error_get_pretty(err));
        return 1;
    }

    g_assert(pts != NULL);
    g_assert(pts->x == 42);
    g_assert(pts->y == 84);

    qobject_decref(obj);

    obj = qobject_from_json("[{'x': 42, 'y': 84}, {'x': 12, 'y': 24}]");
    mi = qmp_input_visiter_new(obj);
    v = qmp_input_get_visiter(mi);

    visit_type_TestStructList(v, &lts, "lts", &err);
    if (err) {
        printf("%s\n", error_get_pretty(err));
        return 1;
    }

    g_assert(lts != NULL);
    g_assert(lts->value->x == 42);
    g_assert(lts->value->y == 84);

    lts = lts->next;
    g_assert(lts != NULL);
    g_assert(lts->value->x == 12);
    g_assert(lts->value->y == 24);

    g_assert(lts->next == NULL);

    qobject_decref(obj);

    g_test_run();

    return 0;
}
