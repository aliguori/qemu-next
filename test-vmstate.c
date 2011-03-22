/*
 * VMState test case
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <glib.h>
#include "qemu-objects.h"

/* TODO
 * 1) verify subsection semantics
 */

extern const char *vmstate_schema_json;

static void compare_section(const char *device, QDict *old, QDict *new)
{
    const QDictEntry *e;
    int old_version, new_version;

    old_version = qdict_get_int(old, "__version__");
    new_version = qdict_get_int(new, "__version__");

    if (old_version != new_version) {
        g_error("Version %d of device `%s' is available in QEMU, but schema still reports %d, please update schema.\n",
                new_version, device, old_version);
    }

    for (e = qdict_first(new); e; e = qdict_next(new, e)) {
        QObject *old_field, *new_field;

        if (strcmp(e->key, "__version__") == 0) {
            continue;
        }

        if (!qdict_haskey(old, e->key)) {
            g_error("You broke migration by adding a new field, `%s', to device `%s', version %d, without changing version.\n",
                    e->key, device, new_version);
        }

        old_field = qdict_get(old, e->key);
        new_field = qdict_get(new, e->key);

        if (qobject_type(old_field) != qobject_type(new_field)) {
            g_error("You broke migration by changing the type of field, `%s', in device `%s', version %d, without changing version.\n",
                    e->key, device, new_version);
        }

        if (qobject_type(old_field) == QTYPE_QSTRING) {
            const char *old_field_type, *new_field_type;

            old_field_type = qdict_get_str(old, e->key);
            new_field_type = qdict_get_str(new, e->key);

            if (strcmp(old_field_type, new_field_type) != 0) {
                g_error("You broke migration by changing the type of field, `%s' in device `%s', version %d from `%s' to `%s'\n",
                        e->key, device, new_version, old_field_type, new_field_type);
            }
        } else {
            char buffer[1024];

            snprintf(buffer, sizeof(buffer), "%s.%s", device, e->key);
            compare_section(buffer, qobject_to_qdict(old_field), qobject_to_qdict(new_field));
        }
    }

    for (e = qdict_first(old); e; e = qdict_next(old, e)) {
        if (!qdict_haskey(new, e->key)) {
            g_error("You broke migration by removing the field, `%s', from device `%s', version %d, without changing version.\n",
                    e->key, device, new_version);
        }
    }
}

static void compare_schema(QDict *old, QDict *new)
{
    const QDictEntry *e;

    for (e = qdict_first(new); e; e = qdict_next(new, e)) {
        if (!qdict_haskey(old, e->key)) {
            g_error("New device introduced, '%s', that's not in current schema.  Please update.\n", e->key);
        }

        compare_section(e->key, qobject_to_qdict(qdict_get(old, e->key)),
                        qobject_to_qdict(qdict_get(new, e->key)));
    }
}

static QObject *read_current_schema(void)
{
    char buffer[65536];
    int fd;
    int ret;
    size_t offset = 0;
    ssize_t len;

    ret = system("i386-softmmu/qemu -vmstate-dump > /tmp/schema.json");
    g_assert_cmpint(ret, ==, 0);

    fd = open("/tmp/schema.json", O_RDONLY);
    g_assert_cmpint(fd, !=, -1);

    do {
        len = read(fd, buffer + offset, sizeof(buffer) - 1 - offset);
        offset += len;
    } while (len > 0);

    buffer[offset] = 0;

    close(fd);

    return qobject_from_json(buffer);
}

static void test_missing_fields(void)
{
    QObject *schema = qobject_from_json(vmstate_schema_json);
    QObject *cur_schema = read_current_schema();

    compare_schema(qobject_to_qdict(schema), qobject_to_qdict(cur_schema));

    qobject_decref(schema);
    qobject_decref(cur_schema);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/latest/schema-change", test_missing_fields);

    g_test_run();

    return 0;
}
