/*
 * QDict unit-tests.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include <glib.h>

#include "qint.h"
#include "qdict.h"
#include "qstring.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void test_qdict_new(void)
{
    QDict *qdict;

    qdict = qdict_new();
    g_assert(qdict != NULL);
    g_assert(qdict_size(qdict) == 0);
    g_assert(qdict->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qdict)) == QTYPE_QDICT);

    // destroy doesn't exit yet
    free(qdict);
}

static void test_qdict_put_obj(void)
{
    QInt *qi;
    QDict *qdict;
    QDictEntry *ent;
    const int num = 42;

    qdict = qdict_new();

    // key "" will have tdb hash 12345
    qdict_put_obj(qdict, "", QOBJECT(qint_from_int(num)));

    g_assert(qdict_size(qdict) == 1);
    ent = QLIST_FIRST(&qdict->table[12345 % QDICT_BUCKET_MAX]);
    qi = qobject_to_qint(ent->value);
    g_assert(qint_get_int(qi) == num);

    // destroy doesn't exit yet
    QDECREF(qi);
    qemu_free(ent->key);
    qemu_free(ent);
    qemu_free(qdict);
}

static void test_qdict_destroy_simple(void)
{
    QDict *qdict;

    qdict = qdict_new();
    qdict_put_obj(qdict, "num", QOBJECT(qint_from_int(0)));
    qdict_put_obj(qdict, "str", QOBJECT(qstring_from_str("foo")));

    QDECREF(qdict);
}

static void test_qdict_get(void)
{
    QInt *qi;
    QObject *obj;
    const int value = -42;
    const char *key = "test";
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    qdict_put(tests_dict, key, qint_from_int(value));

    obj = qdict_get(tests_dict, key);
    g_assert(obj != NULL);

    qi = qobject_to_qint(obj);
    g_assert(qint_get_int(qi) == value);

    QDECREF(tests_dict);
}

static void test_qdict_get_int(void)
{
    int ret;
    const int value = 100;
    const char *key = "int";
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    qdict_put(tests_dict, key, qint_from_int(value));

    ret = qdict_get_int(tests_dict, key);
    g_assert(ret == value);

    QDECREF(tests_dict);
}

static void test_qdict_get_try_int(void)
{
    int ret;
    const int value = 100;
    const char *key = "int";
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    qdict_put(tests_dict, key, qint_from_int(value));

    ret = qdict_get_try_int(tests_dict, key, 0);
    g_assert(ret == value);

    QDECREF(tests_dict);
}

static void test_qdict_get_str(void)
{
    const char *p;
    const char *key = "key";
    const char *str = "string";
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    qdict_put(tests_dict, key, qstring_from_str(str));

    p = qdict_get_str(tests_dict, key);
    g_assert(p != NULL);
    g_assert(strcmp(p, str) == 0);

    QDECREF(tests_dict);
}

static void test_qdict_get_try_str(void)
{
    const char *p;
    const char *key = "key";
    const char *str = "string";
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    qdict_put(tests_dict, key, qstring_from_str(str));

    p = qdict_get_try_str(tests_dict, key);
    g_assert(p != NULL);
    g_assert(strcmp(p, str) == 0);

    QDECREF(tests_dict);
}

static void test_qdict_haskey_not(void)
{
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    g_assert(qdict_haskey(tests_dict, "test") == 0);

    QDECREF(tests_dict);
}

static void test_qdict_haskey(void)
{
    const char *key = "test";
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    qdict_put(tests_dict, key, qint_from_int(0));
    g_assert(qdict_haskey(tests_dict, key) == 1);

    QDECREF(tests_dict);
}

static void test_qdict_del(void)
{
    const char *key = "key test";
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    qdict_put(tests_dict, key, qstring_from_str("foo"));
    g_assert(qdict_size(tests_dict) == 1);

    qdict_del(tests_dict, key);

    g_assert(qdict_size(tests_dict) == 0);
    g_assert(qdict_haskey(tests_dict, key) == 0);

    QDECREF(tests_dict);
}

static void test_qobject_to_qdict(void)
{
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    g_assert(qobject_to_qdict(QOBJECT(tests_dict)) == tests_dict);

    QDECREF(tests_dict);
}

static void test_qdict_iterapi(void)
{
    int count;
    const QDictEntry *ent;
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    g_assert(qdict_first(tests_dict) == NULL);

    qdict_put(tests_dict, "key1", qint_from_int(1));
    qdict_put(tests_dict, "key2", qint_from_int(2));
    qdict_put(tests_dict, "key3", qint_from_int(3));

    count = 0;
    for (ent = qdict_first(tests_dict); ent; ent = qdict_next(tests_dict, ent)){
        g_assert(qdict_haskey(tests_dict, qdict_entry_key(ent)) == 1);
        count++;
    }

    g_assert(count == qdict_size(tests_dict));

    /* Do it again to test restarting */
    count = 0;
    for (ent = qdict_first(tests_dict); ent; ent = qdict_next(tests_dict, ent)){
        g_assert(qdict_haskey(tests_dict, qdict_entry_key(ent)) == 1);
        count++;
    }

    g_assert(count == qdict_size(tests_dict));

    QDECREF(tests_dict);
}

/*
 * Errors test-cases
 */

static void test_qdict_put_exists(void)
{
    int value;
    const char *key = "exists";
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    qdict_put(tests_dict, key, qint_from_int(1));
    qdict_put(tests_dict, key, qint_from_int(2));

    value = qdict_get_int(tests_dict, key);
    g_assert(value == 2);

    g_assert(qdict_size(tests_dict) == 1);

    QDECREF(tests_dict);
}

static void test_qdict_get_not_exists(void)
{
    QDict *tests_dict = NULL;
    tests_dict = qdict_new();
    g_assert(tests_dict != NULL);

    g_assert(qdict_get(tests_dict, "foo") == NULL);

    QDECREF(tests_dict);
}

/*
 * Stress test-case
 *
 * This is a lot big for a unit-test, but there is no other place
 * to have it.
 */

static void remove_dots(char *string)
{
    char *p = strchr(string, ':');
    if (p)
        *p = '\0';
}

static QString *read_line(FILE *file, char *key)
{
    char value[128];

    if (fscanf(file, "%127s%127s", key, value) == EOF) {
        return NULL;
    }
    remove_dots(key);
    return qstring_from_str(value);
}

#define reset_file(file)    fseek(file, 0L, SEEK_SET)

static void test_qdict_stress(void)
{
    size_t lines;
    char key[128];
    FILE *test_file;
    QDict *qdict;
    QString *value;
    const char *test_file_path = "qdict-test-data.txt";

    test_file = fopen(test_file_path, "r");
    g_assert(test_file != NULL);

    // Create the dict
    qdict = qdict_new();
    g_assert(qdict != NULL);

    // Add everything from the test file
    for (lines = 0;; lines++) {
        value = read_line(test_file, key);
        if (!value)
            break;

        qdict_put(qdict, key, value);
    }
    g_assert(qdict_size(qdict) == lines);

    // Check if everything is really in there
    reset_file(test_file);
    for (;;) {
        const char *str1, *str2;

        value = read_line(test_file, key);
        if (!value)
            break;

        str1 = qstring_get_str(value);

        str2 = qdict_get_str(qdict, key);
        g_assert(str2 != NULL);

        g_assert(strcmp(str1, str2) == 0);

        QDECREF(value);
    }

    // Delete everything
    reset_file(test_file);
    for (;;) {
        value = read_line(test_file, key);
        if (!value)
            break;

        qdict_del(qdict, key);
        QDECREF(value);

        g_assert(qdict_haskey(qdict, key) == 0);
    }
    fclose(test_file);

    g_assert(qdict_size(qdict) == 0);
    QDECREF(qdict);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/qdict_new", test_qdict_new);
    g_test_add_func("/public/qdict_put_obj", test_qdict_put_obj);
    g_test_add_func("/public/qdict_destroy_simple", test_qdict_destroy_simple);

    g_test_add_func("/public/qdict_get", test_qdict_get);
    g_test_add_func("/public/qdict_get_int", test_qdict_get_int);
    g_test_add_func("/public/qdict_get_try_int", test_qdict_get_try_int);
    g_test_add_func("/public/qdict_get_str", test_qdict_get_str);
    g_test_add_func("/public/qdict_get_try_str", test_qdict_get_try_str);
    g_test_add_func("/public/qdict_haskey_not", test_qdict_haskey_not);
    g_test_add_func("/public/qdict_haskey", test_qdict_haskey);
    g_test_add_func("/public/qdict_del", test_qdict_del);
    g_test_add_func("/public/qobject_to_qdict", test_qobject_to_qdict);
    g_test_add_func("/public/qdict_iterapi", test_qdict_iterapi);

    g_test_add_func("/errors/qdict_put_exists", test_qdict_put_exists);
    g_test_add_func("/errors/qdict_get_not_exists", test_qdict_get_not_exists);

    /* The Big one */
    g_test_add_func("/stress/qdict_stress", test_qdict_stress);

    g_test_run();

    return 0;
}
