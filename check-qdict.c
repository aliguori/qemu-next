/*
 * QDict unit-tests.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * Not really the best unit-testing example (and not developed in a
 * test-driven way), but does the job.
 */
#include <stdio.h>
#include <stdlib.h>
#include <check.h>

#include "qdict.h"

/*
 * Basic API tests
 */

START_TEST(create_test)
{
    QDict *qdict;

    qdict = qdict_new();
    fail_unless(qdict != NULL, "qdict is NULL");
    fail_unless(qdict_size(qdict) == 0, "size is not 0");

    free(qdict);
}
END_TEST

START_TEST(destroy_simple_test)
{
    QDict *qdict;

    qdict = qdict_new();
    fail_unless(qdict != NULL, "qdict is NULL");

    qdict_destroy(qdict);
    fail_unless(qdict_size(qdict) == 0, "size is not 0");
}
END_TEST

static QDict *tests_dict = NULL;

static void qdict_setup(void)
{
    tests_dict = qdict_new();
    fail_unless(tests_dict != NULL, "tests_dict is NULL");
}

static void qdict_teardown(void)
{
    qdict_destroy(tests_dict);
    tests_dict = NULL;
}

/* two tests in one, not good but... */
START_TEST(insert_new_test)
{
    int *ret, value = 42;
    const char *key = "test";

    qdict_add(tests_dict, key, &value);
    fail_unless(qdict_size(tests_dict) == 1);

    ret = qdict_get(tests_dict, key);
    fail_unless(*ret == value);
}
END_TEST

START_TEST(insert_existing_test)
{
    int *ret, value1, value2;
    const char *key = "test";

    value1 = 42;
    qdict_add(tests_dict, key, &value1);
    fail_unless(qdict_size(tests_dict) == 1);

    value2 = 667;
    qdict_add(tests_dict, key, &value2);

    ret = qdict_get(tests_dict, key);
    fail_unless(*ret == value1);
}
END_TEST

START_TEST(exists_not_exists_test)
{
    fail_unless(qdict_exists(tests_dict, "test") == 0);
}
END_TEST

START_TEST(exists_exists_test)
{
    const char *key = "test";

    qdict_add(tests_dict, key, NULL);
    fail_unless(qdict_exists(tests_dict, key) == 1);
}
END_TEST

START_TEST(get_not_exists_test)
{
    fail_unless(qdict_get(tests_dict, "test") == NULL);
}
END_TEST

START_TEST(del_exists_test)
{
    int value = 42;
    const char *key = "test";

    qdict_add(tests_dict, key, &value);
    fail_unless(qdict_size(tests_dict) == 1);

    qdict_del(tests_dict, key);
    fail_unless(qdict_size(tests_dict) == 0);
    fail_unless(qdict_get(tests_dict, key) == NULL);
}
END_TEST

START_TEST(del_not_exists_test)
{
    const char *key = "test";

    qdict_del(tests_dict, key);
    fail_unless(qdict_size(tests_dict) == 0);
    fail_unless(qdict_get(tests_dict, key) == NULL);
}
END_TEST

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

static int read_chars(FILE *file, char *key, char *value)
{
    if (fscanf(file, "%s%s", key, value) == EOF)
        return 1;
    remove_dots(key);
    return 0;
}

#define reset_file(file)    fseek(file, 0L, SEEK_SET)

START_TEST(qdict_stress_test)
{
    size_t i;
    FILE *test_file;
    QDict *qdict;
    char key[128], value[128];

    test_file = fopen("qdict-test-data.txt", "r");
    fail_unless(test_file != NULL);

    // Create the dict
    qdict = qdict_new();
    fail_unless(qdict != NULL);

    // Add everything from the test file
    for (i = 0;; i++) {
        if (read_chars(test_file, key, value))
            break;
        qdict_add(qdict, key, strdup(value));
    }
    fail_unless(qdict_size(qdict) == i);

    // Check if everything is really in there
    reset_file(test_file);
    for (;;) {
        char *p;

        if (read_chars(test_file, key, value))
            break;

        p = qdict_get(qdict, key);
        fail_unless(p != NULL);
        fail_unless(strcmp(p, value) == 0);
    }

    // Delete everything
    reset_file(test_file);
    for (;;) {
        char *p;

        if (read_chars(test_file, key, value))
            break;

        p = qdict_del(qdict, key);
        fail_unless(p != NULL);
        fail_unless(strcmp(p, value) == 0);
        free(p);

        p = qdict_get(qdict, key);
        fail_unless(p == NULL);
    }

    fail_unless(qdict_size(qdict) == 0);
    qdict_destroy(qdict);
}
END_TEST

static Suite *qdict_suite(void)
{
    Suite *s;
    TCase *qdict_api_tcase;
    TCase *qdict_api2_tcase;
    TCase *qdict_stress_tcase;

    s = suite_create("QDict test-suite");

    /* Very basic API test-case */
    qdict_api_tcase = tcase_create("Basic API");
    suite_add_tcase(s, qdict_api_tcase);
    tcase_add_test(qdict_api_tcase, create_test);
    tcase_add_test(qdict_api_tcase, destroy_simple_test);

    /* The same as above, but with fixture */
    qdict_api2_tcase = tcase_create("Basic API 2");
    suite_add_tcase(s, qdict_api2_tcase);
    tcase_add_checked_fixture(qdict_api2_tcase, qdict_setup, qdict_teardown);
    tcase_add_test(qdict_api2_tcase, insert_new_test);
    tcase_add_test(qdict_api2_tcase, insert_existing_test);
    tcase_add_test(qdict_api2_tcase, exists_not_exists_test);
    tcase_add_test(qdict_api2_tcase, exists_exists_test);
    tcase_add_test(qdict_api2_tcase, get_not_exists_test);
    tcase_add_test(qdict_api2_tcase, del_exists_test);
    tcase_add_test(qdict_api2_tcase, del_not_exists_test);

    /* The Big one */
    qdict_stress_tcase = tcase_create("Stress test");
    suite_add_tcase(s, qdict_stress_tcase);
    tcase_add_test(qdict_stress_tcase, qdict_stress_test);

    return s;
}

int main(void)
{
	int nf;
	Suite *s;
	SRunner *sr;

	s = qdict_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
