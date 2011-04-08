#include <glib.h>
#include "qapi/qmp-output-visiter.h"
#include "qemu-objects.h"

typedef struct TestStruct
{
    int64_t x;
    int64_t y;
} TestStruct;

int main(int argc, char **argv)
{
    QmpOutputVisiter *m;
    Visiter *v;
    TestStruct ts = { 42, 82 };
    TestStruct *pts = &ts;
    Error *err = NULL;
    QObject *obj;
    QString *str;

    g_test_init(&argc, &argv, NULL);

    m = qmp_output_visiter_new();
    v = qmp_output_get_visiter(m);

    visit_start_struct(v, (void **)&pts, "TestStruct", "ts", &err);
    visit_type_int(v, &ts.x, "x", &err);
    visit_type_int(v, &ts.y, "y", &err);
    visit_end_struct(v, &err);

    obj = qmp_output_get_qobject(m);

    str = qobject_to_json(obj);

    printf("%s\n", qstring_get_str(str));

    QDECREF(str);

    g_test_run();

    return 0;
}
