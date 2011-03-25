/*
 * QFloat unit-tests.
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#include <glib.h>

#include "qfloat.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void test_qfloat_from_double(void)
{
    QFloat *qf;
    const double value = -42.23423;

    qf = qfloat_from_double(value);
    g_assert(qf != NULL);
    g_assert(qf->value == value);
    g_assert(qf->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qf)) == QTYPE_QFLOAT);

    qemu_free(qf);
}

static void test_qfloat_destroy(void)
{
    QFloat *qf = qfloat_from_double(0.0);
    QDECREF(qf);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/qfloat_from_double", test_qfloat_from_double);
    g_test_add_func("/public/qfloat_destroy", test_qfloat_destroy);

    g_test_run();

    return 0;
}
