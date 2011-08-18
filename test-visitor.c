#include <glib.h>
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp-input-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qemu-objects.h"
#include "qapi/qemu-file-output-visitor.h"
#include "qapi/qemu-file-input-visitor.h"
#include "hw/hw.h"
#include "qemu-common.h"

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

static void visit_type_TestStruct(Visitor *v, TestStruct **obj, const char *name, Error **errp)
{
    visit_start_struct(v, (void **)obj, "TestStruct", name, sizeof(TestStruct), errp);
    visit_type_int(v, &(*obj)->x, "x", errp);
    visit_type_int(v, &(*obj)->y, "y", errp);
    visit_end_struct(v, errp);
}

static void visit_type_TestStructList(Visitor *m, TestStructList ** obj, const char *name, Error **errp)
{
    GenericList *i, **head = (GenericList **)obj;

    visit_start_list(m, name, errp);

    for (*head = i = visit_next_list(m, head, errp); i; i = visit_next_list(m, &i, errp)) {
        TestStructList *native_i = (TestStructList *)i;
        visit_type_TestStruct(m, &native_i->value, NULL, errp);
    }

    visit_end_list(m, errp);
}

/* test core visitor methods */
static void test_visitor_core(void)
{
    QmpOutputVisitor *mo;
    QmpInputVisitor *mi;
    Visitor *v;
    TestStruct ts = { 42, 82 };
    TestStruct *pts = &ts;
    TestStructList *lts = NULL;
    Error *err = NULL;
    QObject *obj;
    QList *qlist;
    QDict *qdict;
    QString *str;
    int64_t value = 0;

    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);

    visit_type_TestStruct(v, &pts, NULL, &err);

    obj = qmp_output_get_qobject(mo);

    str = qobject_to_json(obj);

    printf("%s\n", qstring_get_str(str));

    QDECREF(str);

    obj = QOBJECT(qint_from_int(0x42));

    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);

    visit_type_int(v, &value, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }

    g_assert(value == 0x42);

    qobject_decref(obj);

    obj = qobject_from_json("{'x': 42, 'y': 84}");
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);

    pts = NULL;

    visit_type_TestStruct(v, &pts, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }

    g_assert(pts != NULL);
    g_assert(pts->x == 42);
    g_assert(pts->y == 84);

    qobject_decref(obj);
    g_free(pts);

    /* test list input visitor */
    obj = qobject_from_json("[{'x': 42, 'y': 84}, {'x': 12, 'y': 24}]");
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);

    visit_type_TestStructList(v, &lts, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }

    g_assert(lts != NULL);
    g_assert(lts->value->x == 42);
    g_assert(lts->value->y == 84);

    g_assert(lts->next != NULL);
    g_assert(lts->next->value->x == 12);
    g_assert(lts->next->value->y == 24);
    g_assert(lts->next->next == NULL);

    qobject_decref(obj);

    /* test list output visitor */
    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);
    visit_type_TestStructList(v, &lts, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    obj = qmp_output_get_qobject(mo);
    g_print("obj: %s\n", qstring_get_str(qobject_to_json(obj)));

    qlist = qobject_to_qlist(obj);
    assert(qlist);
    obj = qlist_pop(qlist);
    qdict = qobject_to_qdict(obj);
    assert(qdict);
    assert(qdict_get_int(qdict, "x") == 42);
    assert(qdict_get_int(qdict, "y") == 84);
    qobject_decref(obj);

    obj = qlist_pop(qlist);
    qdict = qobject_to_qdict(obj);
    assert(qdict);
    assert(qdict_get_int(qdict, "x") == 12);
    assert(qdict_get_int(qdict, "y") == 24);
    qobject_decref(obj);

    qmp_output_visitor_cleanup(mo);
    QDECREF(qlist);
}

/* test deep nesting with refs to other user-defined types */
static void test_nested_structs(void)
{
    QmpOutputVisitor *mo;
    QmpInputVisitor *mi;
    Visitor *v;
    UserDefOne ud1;
    UserDefOne *ud1_p = &ud1, *ud1c_p = NULL;
    UserDefTwo ud2;
    UserDefTwo *ud2_p = &ud2, *ud2c_p = NULL;
    Error *err = NULL;
    QObject *obj;
    QString *str;

    ud1.integer = 42;
    ud1.string = strdup("fourty two");

    /* sanity check */
    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);
    visit_type_UserDefOne(v, &ud1_p, "o_O", &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    obj = qmp_output_get_qobject(mo);
    g_assert(obj);
    qobject_decref(obj);

    ud2.string = strdup("fourty three");
    ud2.dict.string = strdup("fourty four");
    ud2.dict.dict.userdef = ud1_p;
    ud2.dict.dict.string = strdup("fourty five");
    ud2.dict.has_dict2 = true;
    ud2.dict.dict2.userdef = ud1_p;
    ud2.dict.dict2.string = strdup("fourty six");

    /* c type -> qobject */
    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);
    visit_type_UserDefTwo(v, &ud2_p, "unused", &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    obj = qmp_output_get_qobject(mo);
    g_assert(obj);
    str = qobject_to_json_pretty(obj);
    g_print("%s\n", qstring_get_str(str));
    QDECREF(str);

    /* qobject -> c type, should match original struct */
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);
    visit_type_UserDefTwo(v, &ud2c_p, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }

    g_assert(!g_strcmp0(ud2c_p->string, ud2.string));
    g_assert(!g_strcmp0(ud2c_p->dict.string, ud2.dict.string));

    ud1c_p = ud2c_p->dict.dict.userdef;
    g_assert(ud1c_p->integer == ud1_p->integer);
    g_assert(!g_strcmp0(ud1c_p->string, ud1_p->string));

    g_assert(!g_strcmp0(ud2c_p->dict.dict.string, ud2.dict.dict.string));

    ud1c_p = ud2c_p->dict.dict2.userdef;
    g_assert(ud1c_p->integer == ud1_p->integer);
    g_assert(!g_strcmp0(ud1c_p->string, ud1_p->string));

    g_assert(!g_strcmp0(ud2c_p->dict.dict2.string, ud2.dict.dict2.string));
    g_free(ud1.string);
    g_free(ud2.string);
    g_free(ud2.dict.string);
    g_free(ud2.dict.dict.string);
    g_free(ud2.dict.dict2.string);

    qapi_free_UserDefTwo(ud2c_p);

    qobject_decref(obj);
}

/* test enum values */
static void test_enums(void)
{
    QmpOutputVisitor *mo;
    QmpInputVisitor *mi;
    Visitor *v;
    EnumOne enum1 = ENUM_ONE_VALUE2, enum1_cpy = ENUM_ONE_VALUE1;
    Error *err = NULL;
    QObject *obj;
    QString *str;

    /* C type -> QObject */
    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);
    visit_type_EnumOne(v, &enum1, "unused", &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    obj = qmp_output_get_qobject(mo);
    g_assert(obj);
    str = qobject_to_json_pretty(obj);
    g_print("%s\n", qstring_get_str(str));
    QDECREF(str);
    g_assert(g_strcmp0(qstring_get_str(qobject_to_qstring(obj)), "value2") == 0);

    /* QObject -> C type */
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);
    visit_type_EnumOne(v, &enum1_cpy, "unused", &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    g_debug("enum1_cpy, enum1: %d, %d", enum1_cpy, enum1);
    g_assert(enum1_cpy == enum1);

    qobject_decref(obj);
}

/* test enum values nested in schema-defined structs */
static void test_nested_enums(void)
{
    QmpOutputVisitor *mo;
    QmpInputVisitor *mi;
    Visitor *v;
    NestedEnumsOne *nested_enums, *nested_enums_cpy = NULL;
    Error *err = NULL;
    QObject *obj;
    QString *str;

    nested_enums = g_malloc0(sizeof(NestedEnumsOne));
    nested_enums->enum1 = ENUM_ONE_VALUE1;
    nested_enums->enum2 = ENUM_ONE_VALUE2;
    nested_enums->enum3 = ENUM_ONE_VALUE3;
    nested_enums->enum4 = ENUM_ONE_VALUE3;
    nested_enums->has_enum2 = false;
    nested_enums->has_enum4 = true;

    /* C type -> QObject */
    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);
    visit_type_NestedEnumsOne(v, &nested_enums, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    obj = qmp_output_get_qobject(mo);
    g_assert(obj);
    str = qobject_to_json_pretty(obj);
    g_print("%s\n", qstring_get_str(str));
    QDECREF(str);

    /* QObject -> C type */
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);
    visit_type_NestedEnumsOne(v, &nested_enums_cpy, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    g_assert(nested_enums_cpy);
    g_assert(nested_enums_cpy->enum1 == nested_enums->enum1);
    g_assert(nested_enums_cpy->enum3 == nested_enums->enum3);
    g_assert(nested_enums_cpy->enum4 == nested_enums->enum4);
    g_assert(nested_enums_cpy->has_enum2 == false);
    g_assert(nested_enums_cpy->has_enum4 == true);

    qmp_output_visitor_cleanup(mo);
    qmp_input_visitor_cleanup(mi);
    qapi_free_NestedEnumsOne(nested_enums);
    qapi_free_NestedEnumsOne(nested_enums_cpy);
}

#define TEST_QEMU_FILE_PATH "/tmp/test_qemu_file_visitors"

typedef struct QEMUFileValue {
    union {
        bool boolean;
        double number;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        int8_t s8;
        int16_t s16;
        int32_t s32;
        int64_t s64;
        uintmax_t umax;
    } value;
    union {
        uint8_t u8[32];
        uint16_t u16[32];
        uint32_t u32[32];
        uint64_t u64[32];
        int8_t s8[32];
        int16_t s16[32];
        int32_t s32[32];
        int64_t s64[32];
        uintmax_t umax[32];
    } array;
    size_t array_len;
    enum {
        QFV_BOOL = 0,
        QFV_NUMBER,
        QFV_U8,
        QFV_U16,
        QFV_U32,
        QFV_U64,
        QFV_S8,
        QFV_S16,
        QFV_S32,
        QFV_S64,
        QFV_U8_ARRAY,
        QFV_EOL,
    } type;
} QEMUFileValue;

QEMUFileValue qfvalues[] = {
    { .value.boolean = true, .type = QFV_BOOL },
    { .value.boolean = false, .type = QFV_BOOL },
    { .value.number = 3.14159265, .type = QFV_NUMBER },
    { .value.u8 = 0, .type = QFV_U8 },
    { .value.u8 = 1, .type = QFV_U8 },
    { .value.u8 = 128, .type = QFV_U8 },
    { .value.u8 = 255u, .type = QFV_U8 },
    { .value.u16 = 0, .type = QFV_U16 },
    { .value.u16 = 1, .type = QFV_U16 },
    { .value.u16 = 32768, .type = QFV_U16 },
    { .value.u16 = 65535u, .type = QFV_U16 },
    { .value.u32 = 0, .type = QFV_U32 },
    { .value.u32 = 1, .type = QFV_U32 },
    { .value.u32 = 2147483648, .type = QFV_U32 },
    { .value.u32 = 4294967295u, .type = QFV_U32 },
    { .value.u64 = 0, .type = QFV_U64 },
    { .value.u64 = 1, .type = QFV_U64 },
    { .value.u64 = 9223372036854775808u, .type = QFV_U64 },
    { .value.u64 = 18446744073709551615u, .type = QFV_U64 },
    { .value.s8 = 0, .type = QFV_S8 },
    { .value.s8 = 1, .type = QFV_S8 },
    { .value.s8 = 128, .type = QFV_S8 },
    { .value.s8 = -1, .type = QFV_S8 },
    { .value.s16 = 0, .type = QFV_S16 },
    { .value.s16 = 1, .type = QFV_S16 },
    { .value.s16 = 32768, .type = QFV_S16 },
    { .value.s16 = -1, .type = QFV_S16 },
    { .value.s32 = 0, .type = QFV_S32 },
    { .value.s32 = 1, .type = QFV_S32 },
    { .value.s32 = 2147483648, .type = QFV_S32 },
    { .value.s32 = -1, .type = QFV_S32 },
    { .value.s64 = 0, .type = QFV_S64 },
    { .value.s64 = 1, .type = QFV_S64 },
    { .value.s64 = 9223372036854775808u, .type = QFV_S64 },
    { .value.s64 = -1, .type = QFV_S64 },
    { .array.u8 = { }, .array_len = 0, .type = QFV_U8_ARRAY },
    { .array.u8 = { 1, 2, 3, 4 }, .array_len = 4, .type = QFV_U8_ARRAY },
    { .type = QFV_EOL }
};

static void qfv_process(QEMUFileValue *qfv, bool visitor, bool write,
                        void *opaque)
{
    int i;
    void *ptr;

    switch (qfv->type) {
    case QFV_BOOL:
        if (visitor) {
            visit_type_bool(opaque, &qfv->value.boolean, NULL, NULL);
        } else {
            if (write) {
                qemu_put_byte(opaque, qfv->value.boolean);
            } else {
                qfv->value.boolean = qemu_get_byte(opaque);
            }
        }
        break;
    case QFV_NUMBER:
        if (visitor) {
            visit_type_number(opaque, &qfv->value.number, NULL, NULL);
        } else {
            if (write) {
                qemu_put_be64s(opaque, (uint64_t *)&qfv->value.number);
            } else {
                qemu_get_be64s(opaque, (uint64_t *)&qfv->value.number);
            }
        }
        break;
    case QFV_U8:
        if (visitor) {
            visit_type_uint8(opaque, &qfv->value.u8, NULL, NULL);
        } else {
            if (write) {
                qemu_put_byte(opaque, qfv->value.u8);
            } else {
                qfv->value.u8 = qemu_get_byte(opaque);
            }
        }
        break;
    case QFV_U16:
        if (visitor) {
            visit_type_uint16(opaque, &qfv->value.u16, NULL, NULL);
        } else {
            if (write) {
                qemu_put_be16(opaque, qfv->value.u16);
            } else {
                qfv->value.u16 = qemu_get_be16(opaque);
            }
        }
        break;
    case QFV_U32:
        if (visitor) {
            visit_type_uint32(opaque, &qfv->value.u32, NULL, NULL);
        } else {
            if (write) {
                qemu_put_be32(opaque, qfv->value.u32);
            } else {
                qfv->value.u32 = qemu_get_be32(opaque);
            }
        }
        break;
    case QFV_U64:
        if (visitor) {
            visit_type_uint64(opaque, &qfv->value.u64, NULL, NULL);
        } else {
            if (write) {
                qemu_put_be64(opaque, qfv->value.u64);
            } else {
                qfv->value.u64 = qemu_get_be64(opaque);
            }
        }
        break;
    case QFV_S8:
        if (visitor) {
            visit_type_int8(opaque, &qfv->value.s8, NULL, NULL);
        } else {
            if (write) {
                qemu_put_byte(opaque, qfv->value.s8);
            } else {
                qfv->value.s8 = qemu_get_byte(opaque);
            }
        }
        break;
    case QFV_S16:
        if (visitor) {
            visit_type_int16(opaque, &qfv->value.s16, NULL, NULL);
        } else {
            if (write) {
                qemu_put_be16(opaque, qfv->value.s16);
            } else {
                qfv->value.s16 = qemu_get_be16(opaque);
            }
        }
        break;
    case QFV_S32:
        if (visitor) {
            visit_type_int32(opaque, &qfv->value.s32, NULL, NULL);
        } else {
            if (write) {
                qemu_put_be32(opaque, qfv->value.s32);
            } else {
                qfv->value.s32 = qemu_get_be32(opaque);
            }
        }
        break;
    case QFV_S64:
        if (visitor) {
            visit_type_int64(opaque, &qfv->value.s64, NULL, NULL);
        } else {
            if (write) {
                qemu_put_be64(opaque, qfv->value.s64);
            } else {
                qfv->value.s64 = qemu_get_be64(opaque);
            }
        }
        break;
    case QFV_U8_ARRAY:
        if (visitor) {
            ptr = qfv->array.u8;
            visit_start_array(opaque, (void **)&ptr, NULL,
                              qfv->array_len, sizeof(uint8_t), NULL);
            for (i = 0; i < qfv->array_len; i++, visit_next_array(opaque, NULL)) {
                visit_type_uint8(opaque, &((uint8_t *)ptr)[i], NULL, NULL);
            }
            visit_end_array(opaque, NULL);
        } else {
            for (i = 0; i < qfv->array_len; i++) {
                if (write) {
                    qemu_put_byte(opaque, qfv->array.u8[i]);
                } else {
                    qfv->array.u8[i] = qemu_get_byte(opaque);
                }
            }
        }
        break;
    default:
        return;
    }
}

static void qfv_visitor_write(QEMUFileValue *qfv, Visitor *v)
{
    qfv_process(qfv, true, true, v);
}

static void qfv_visitor_read(QEMUFileValue *qfv, Visitor *v)
{
    qfv_process(qfv, true, false, v);
}

static void qfv_write(QEMUFileValue *qfv, QEMUFile *f)
{
    qfv_process(qfv, false, true, f);
}

static void qfv_read(QEMUFileValue *qfv, QEMUFile *f)
{
    qfv_process(qfv, false, false, f);
}

static void test_qemu_file_in_visitor(void)
{
    QEMUFile *f1, *f2;
    QemuFileInputVisitor *qfi;
    QemuFileOutputVisitor *qfo;
    Visitor *v;
    QEMUFileValue qfval1, qfval2;
    int i, j;
    TestStruct ts, *pts;
    TestStructList *lts;

    /* write our test scalars/arrays */
    f1 = qemu_fopen(TEST_QEMU_FILE_PATH, "wb");
    g_assert(f1);
    qfo = qemu_file_output_visitor_new(f1);
    v = qemu_file_output_get_visitor(qfo);
    for (i = 0; qfvalues[i].type != QFV_EOL; i++) {
        qfv_write(&qfvalues[i], f1);
    }
    /* write our test struct/list. qemu_put_* interfaces have
     * no analogue for this and instead rely on byte arrays,
     * so we'll write this using a visitor and simply test
     * visitor input/output compatibility
     */
    /* write a simple struct */
    ts.x = 42;
    ts.y = 43;
    pts = &ts;
    visit_type_TestStruct(v, &pts, NULL, NULL);
    /* throw in a linked list as well */
    lts = g_malloc0(sizeof(*lts));
    lts->value = g_malloc0(sizeof(TestStruct));
    lts->value->x = 44;
    lts->value->y = 45;
    lts->next = g_malloc0(sizeof(*lts));
    lts->next->value = g_malloc0(sizeof(TestStruct));
    lts->next->value->x = 46;
    lts->next->value->y = 47;
    visit_type_TestStructList(v, &lts, NULL, NULL);
    g_free(lts->next->value);
    g_free(lts->next);
    g_free(lts->value);
    g_free(lts);

    qemu_file_output_visitor_cleanup(qfo);
    qemu_fclose(f1);

    /* make sure qemu_get_be* and input visitor read same/correct input */
    f1 = qemu_fopen(TEST_QEMU_FILE_PATH, "rb");
    f2 = qemu_fopen(TEST_QEMU_FILE_PATH, "rb");
    qfi = qemu_file_input_visitor_new(f2);
    g_assert(qfi);
    v = qemu_file_input_get_visitor(qfi);
    g_assert(v);
    for (i = 0; qfvalues[i].type != QFV_EOL; i++) {
        qfval1.value.umax = qfval2.value.umax = 0;
        memset(qfval1.array.umax, 0, sizeof(qfval1.array.umax));
        memset(qfval2.array.umax, 0, sizeof(qfval2.array.umax));
        qfval1.type = qfval2.type = qfvalues[i].type;
        qfval1.array_len = qfval2.array_len = qfvalues[i].array_len;
        qfv_read(&qfval1, f1);
        qfv_visitor_read(&qfval2, v);
        if (qfvalues[i].type >= QFV_U8_ARRAY) {
            for (j = 0; j < qfvalues[i].array_len; j++) { 
                g_assert(qfval1.array.u8[j] == qfval2.array.u8[j]);
                g_assert(qfval2.array.u8[j] == qfvalues[i].array.u8[j]);
            }
        } else {
            g_assert(qfval1.value.umax == qfval2.value.umax);
            g_assert(qfval2.value.umax == qfvalues[i].value.umax);
        }
    }
    qemu_file_input_visitor_cleanup(qfi);
    qemu_fclose(f1);
    qemu_fclose(f2);
    unlink(TEST_QEMU_FILE_PATH);
}

static void test_qemu_file_out_visitor(void)
{
    QEMUFile *f;
    QemuFileOutputVisitor *qfo;
    Visitor *v;
    QEMUFileValue qfval1;
    int i, j;
    TestStruct ts, *pts;
    TestStructList *lts;

    /* write test scalars/arrays using an output visitor */
    f = qemu_fopen(TEST_QEMU_FILE_PATH, "wb");
    g_assert(f);
    qfo = qemu_file_output_visitor_new(f);
    g_assert(qfo);
    v = qemu_file_output_get_visitor(qfo);
    g_assert(v);
    for (i = 0; qfvalues[i].type != QFV_EOL; i++) {
        qfv_visitor_write(&qfvalues[i], v);
    }
    /* write a simple struct */
    ts.x = 42;
    ts.y = 43;
    pts = &ts;
    visit_type_TestStruct(v, &pts, NULL, NULL);
    /* throw in a linked list as well */
    lts = g_malloc0(sizeof(*lts));
    lts->value = g_malloc0(sizeof(TestStruct));
    lts->value->x = 44;
    lts->value->y = 45;
    lts->next = g_malloc0(sizeof(*lts));
    lts->next->value = g_malloc0(sizeof(TestStruct));
    lts->next->value->x = 46;
    lts->next->value->y = 47;
    visit_type_TestStructList(v, &lts, NULL, NULL);
    g_free(lts->next->value);
    g_free(lts->next);
    g_free(lts->value);
    g_free(lts);

    qemu_file_output_visitor_cleanup(qfo);
    qemu_fclose(f);

    /* make sure output visitor wrote the expected values */
    f = qemu_fopen(TEST_QEMU_FILE_PATH, "rb");
    g_assert(f);
    for (i = 0; qfvalues[i].type != QFV_EOL; i++) {
        qfval1.type = qfvalues[i].type;
        qfval1.value.umax = 0;
        memset(qfval1.array.umax, 0, sizeof(qfval1.array.umax));
        qfval1.array_len = qfvalues[i].array_len;

        qfv_read(&qfval1, f);
        if (qfvalues[i].type >= QFV_U8_ARRAY) {
            for (j = 0; j < qfvalues[i].array_len; j++) { 
                g_assert(qfval1.array.u8[j] == qfvalues[i].array.u8[j]);
            }
        } else {
            g_assert(qfval1.value.umax == qfvalues[i].value.umax);
        }
    }
    /* test the struct */
    g_assert(qemu_get_be64(f) == ts.x);
    g_assert(qemu_get_be64(f) == ts.y);
    /* test the linked list */
    g_assert(qemu_get_be64(f) == 44);
    g_assert(qemu_get_be64(f) == 45);
    g_assert(qemu_get_be64(f) == 46);
    g_assert(qemu_get_be64(f) == 47);

    qemu_fclose(f);
    unlink(TEST_QEMU_FILE_PATH);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/0.15/visitor_core", test_visitor_core);
    g_test_add_func("/0.15/nested_structs", test_nested_structs);
    g_test_add_func("/0.15/enums", test_enums);
    g_test_add_func("/0.15/nested_enums", test_nested_enums);
    g_test_add_func("/1.0/qemu_file_input_visitor", test_qemu_file_in_visitor);
    g_test_add_func("/1.0/qemu_file_output_visitor", test_qemu_file_out_visitor);

    g_test_run();

    return 0;
}
