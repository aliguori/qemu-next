#include "qcfg-core.h"
#include "qcfg-marshal.h"
#include <glib.h>

static void test_blockdev(void)
{
    KeyValues *kvs;
    BlockdevConfig *config;
    const char *fmt =
        "id=ide0-hd0,"
        "format.qcow2.protocol.file.filename=image.img";
    Error *err = NULL;

    kvs = qcfg_parse(fmt, "format.probe.protocol.probe.filename");
    g_assert(kvs != NULL);

    config = qcfg_unmarshal_type_BlockdevConfig(kvs, &err);
    if (err) {
        printf("%s\n", error_get_pretty(err));
    }
    g_assert(err == NULL);

    g_assert_cmpstr(config->id, ==, "ide0-hd0");
    g_assert(config->has_cache == false);
    g_assert(config->has_device == false);

    g_assert(config->format->kind == BFK_QCOW2);
    g_assert(config->format->qcow2->has_backing_file == false);
    g_assert_cmpint(config->format->qcow2->protocol->kind, ==, BPK_FILE);
    g_assert_cmpstr(config->format->qcow2->protocol->file->filename, ==, "image.img");

    qapi_free_blockdev_config(config);

    qapi_free_key_values(kvs);
}

int main(int argc, char **argv)
{
    int i;

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/blockdev", test_blockdev);

    g_test_run();

    for (i = 1; i < argc; i++) {
    }

    return 0;
}
