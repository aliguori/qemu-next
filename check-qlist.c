/*
 * QList unit-tests.
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
#include "qlist.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void test_qlist_new(void)
{
    QList *qlist;

    qlist = qlist_new();
    g_assert(qlist != NULL);
    g_assert(qlist->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qlist)) == QTYPE_QLIST);

    // destroy doesn't exist yet
    qemu_free(qlist);
}

static void test_qlist_append(void)
{
    QInt *qi;
    QList *qlist;
    QListEntry *entry;

    qi = qint_from_int(42);

    qlist = qlist_new();
    qlist_append(qlist, qi);

    entry = QTAILQ_FIRST(&qlist->head);
    g_assert(entry != NULL);
    g_assert(entry->value == QOBJECT(qi));

    // destroy doesn't exist yet
    QDECREF(qi);
    qemu_free(entry);
    qemu_free(qlist);
}

static void test_qobject_to_qlist(void)
{
    QList *qlist;

    qlist = qlist_new();

    g_assert(qobject_to_qlist(QOBJECT(qlist)) == qlist);

    // destroy doesn't exist yet
    qemu_free(qlist);
}

static void test_qlist_destroy(void)
{
    int i;
    QList *qlist;

    qlist = qlist_new();

    for (i = 0; i < 42; i++)
        qlist_append(qlist, qint_from_int(i));

    QDECREF(qlist);
}

static int iter_called;
static const int iter_max = 42;

static void iter_func(QObject *obj, void *opaque)
{
    QInt *qi;

    g_assert(opaque == NULL);

    qi = qobject_to_qint(obj);
    g_assert(qi != NULL);
    g_assert((qint_get_int(qi) >= 0) && (qint_get_int(qi) <= iter_max));

    iter_called++;
}

static void test_qlist_iter(void)
{
    int i;
    QList *qlist;

    qlist = qlist_new();

    for (i = 0; i < iter_max; i++)
        qlist_append(qlist, qint_from_int(i));

    iter_called = 0;
    qlist_iter(qlist, iter_func, NULL);

    g_assert(iter_called == iter_max);

    QDECREF(qlist);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/qlist_new", test_qlist_new);
    g_test_add_func("/public/qlist_append", test_qlist_append);
    g_test_add_func("/public/qobject_to_qlist", test_qobject_to_qlist);
    g_test_add_func("/public/qlist_destroy", test_qlist_destroy);
    g_test_add_func("/public/qlist_iter", test_qlist_iter);

    g_test_run();
    
    return 0;
}
