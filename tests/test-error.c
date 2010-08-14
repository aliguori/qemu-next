/*
 * QEMU Errors
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori <aliguori@us.ibm.com>
 *
 */

#include "qemu-common.h"
#include "error.h"

#define fail(msg, ...) fprintf(stderr, msg, ## __VA_ARGS__)
#define _(str) (char *)(str)

static Error test_data[] = {
    { _("test-error"), 0, _("This was a simple error") },
    { _("test-error"), 0, _("This was a not so simple error") },
    { _("foo"), -42, _("This is a complex error") },
    { }
};

static void print_error(const Error *err)
{
    fail("{\"%s\", %d, \"%s\"}", err->domain, err->code, err->message);
}

static int cmp_error(const Error *lhs, const Error *rhs)
{
    int res;

    res = strcmp(lhs->domain, rhs->domain);
    if (res != 0) {
        return res;
    }

    if (lhs->code != rhs->code) {
        return lhs->code - rhs->code;
    }

    res = strcmp(lhs->message, rhs->message);
    if (res != 0) {
        return res;
    }

    return 0;
}

static int check_error(const Error *lhs, const Error *rhs)
{
    if (cmp_error(lhs, rhs) != 0) {
        fail("FAILED: unexpected error contents\n");
        fail(" expected: ");
        print_error(rhs);
        fail("\n");
        fail(" actual:   ");
        print_error(lhs);
        fail("\n");
        return 0;
    }

    return 1;
}

int main(int argc, char **argv)
{
    Error *err;
    int i;

    err = error_new("test-error", 0, "This was a simple error");
    if (!check_error(err, &test_data[0])) {
        return 1;
    }
    error_free(err);

    err = error_new("test-error", 0, "This was a %s simple error", "not so");
    if (!check_error(err, &test_data[1])) {
        return 1;
    }
    error_free(err);

    err = error_new("foo", -42, "This is a %s %s", "complex", "error");
    if (!check_error(err, &test_data[2])) {
        return 1;
    }
    error_free(err);

    for (i = 0; test_data[i].domain; i++) {
        Error *cpy;

        err = error_new_literal(test_data[i].domain,
                                test_data[i].code,
                                test_data[i].message);
        if (!check_error(err, &test_data[i])) {
            return 1;
        }

        cpy = error_copy(err);
        error_free(err);

        if (!check_error(cpy, &test_data[i])) {
            return 1;
        }
        error_free(cpy);

        error_set(NULL,
                  test_data[i].domain,
                  test_data[i].code,
                  "%s",
                  test_data[i].message);

        error_set(&err,
                  test_data[i].domain,
                  test_data[i].code,
                  "%s",
                  test_data[i].message);

        if (!check_error(err, &test_data[i])) {
            return 1;
        }

        error_propagate(&cpy, err);
        if (!check_error(cpy, &test_data[i])) {
            return 1;
        }

        error_propagate(NULL, cpy);
    }

    return 0;
}
