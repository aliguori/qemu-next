#include "qcfg-types.h"
#include "qcfg-core.h"
#include "qcfg-marshal.h"

static void print_kv(KeyValues *kvs)
{
    KeyValues *kv;

    printf("KEYS:\n");
    for (kv = kvs; kv; kv = kv->next) {
        printf(" %s: %s\n", kv->key, kv->value);
    }
}

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        KeyValues *kvs;
        BlockdevConfig *config;
        Error *err = NULL;

        kvs = qcfg_parse(argv[i], "foo");
        print_kv(kvs);

        config = qcfg_unmarshal_type_BlockdevConfig(kvs, &err);
        if (err) {
            printf("parse error: %s\n", error_get_pretty(err));
            error_free(err);
            qmp_free_key_values(kvs);
            return 1;
        }

        qmp_free_key_values(kvs);
    }

    return 0;
}
