/*
 * QString unit-tests.
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

#include "qstring.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void test_qstring_from_str(void)
{
    QString *qstring;
    const char *str = "QEMU";

    qstring = qstring_from_str(str);
    g_assert(qstring != NULL);
    g_assert(qstring->base.refcnt == 1);
    g_assert(strcmp(str, qstring->string) == 0);
    g_assert(qobject_type(QOBJECT(qstring)) == QTYPE_QSTRING);

    // destroy doesn't exit yet
    qemu_free(qstring->string);
    qemu_free(qstring);
}

static void test_qstring_destroy(void)
{
    QString *qstring = qstring_from_str("destroy test");
    QDECREF(qstring);
}

static void test_qstring_get_str(void)
{
    QString *qstring;
    const char *ret_str;
    const char *str = "QEMU/KVM";

    qstring = qstring_from_str(str);
    ret_str = qstring_get_str(qstring);
    g_assert(strcmp(ret_str, str) == 0);

    QDECREF(qstring);
}

static void test_qstring_append_chr(void)
{
    int i;
    QString *qstring;
    const char *str = "qstring append char unit-test";

    qstring = qstring_new();

    for (i = 0; str[i]; i++)
        qstring_append_chr(qstring, str[i]);

    g_assert(strcmp(str, qstring_get_str(qstring)) == 0);
    QDECREF(qstring);
}

static void test_qstring_from_substr(void)
{
    QString *qs;

    qs = qstring_from_substr("virtualization", 3, 9);
    g_assert(qs != NULL);
    g_assert(strcmp(qstring_get_str(qs), "tualiza") == 0);

    QDECREF(qs);
}


static void test_qobject_to_qstring(void)
{
    QString *qstring;

    qstring = qstring_from_str("foo");
    g_assert(qobject_to_qstring(QOBJECT(qstring)) == qstring);

    QDECREF(qstring);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/qstring_from_str", test_qstring_from_str);
    g_test_add_func("/public/qstring_destroy", test_qstring_destroy);
    g_test_add_func("/public/qstring_get_str", test_qstring_get_str);
    g_test_add_func("/public/qstring_append_chr", test_qstring_append_chr);
    g_test_add_func("/public/qstring_from_substr", test_qstring_from_substr);
    g_test_add_func("/public/qobject_to_qstring", test_qobject_to_qstring);

    g_test_run();

    return 0;
}
