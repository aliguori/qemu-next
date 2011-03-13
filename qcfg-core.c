#include "qcfg-core.h"
#include "qerror.h"

KeyValues *qcfg_parse(const char *value, const char *implicit_key)
{
    return NULL;
}

KeyValues *qcfg_find_key(KeyValues *kvs, const char *key)
{
    KeyValues *ret = NULL;
    KeyValues *kv;
    const char *p;
    bool single_key = false;
    bool multi_key = false;
    KeyValues **retp = &ret;

    for (kv = kvs; kv; kv = kv->next) {
        if (!multi_key && strcmp(kv->key, key) == 0) {
            ret = kv;
            single_key = true;
        } else if (!single_key && strstart(kv->key, key, &p)) {
            if (*p == '.') {
                KeyValues *new_kv = qmp_alloc_key_values();

                new_kv->key = qemu_strdup(p + 1);
                new_kv->value = qemu_strdup(kv->value);
                *retp = new_kv;
                retp = &new_kv->next;
                multi_key = true;
            }
        }
    }

    if (single_key && ret) {
        KeyValues *new_kv = qmp_alloc_key_values();
        new_kv->key = qemu_strdup(ret->key);
        new_kv->value = qemu_strdup(ret->value);
        ret = new_kv;
    }

    return ret;
}

char *qcfg_unmarshal_type_str(KeyValues *kvs, Error **errp)
{
    return qemu_strdup(kvs->value);
}

bool qcfg_unmarshal_type_bool(KeyValues *kvs, Error **errp)
{
    if (strcmp(kvs->value, "on") == 0) {
        return true;
    } else if (strcmp(kvs->value, "off") == 0) {
        return false;
    }

    error_set(errp, QERR_INVALID_PARAMETER_VALUE, "<unknown>", "a valid boolean");
    return false;
}

int64_t qcfg_unmarshal_type_int(KeyValues *kvs, Error **errp)
{
    int64_t val;
    char *ptr;

    val = strtoll(kvs->value, &ptr, 10);
    if (ptr && *ptr) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "<unknown>", "a valid int");
        return -1;
    }

    return val;
}

double qcfg_unmarshal_type_number(KeyValues *kvs, Error **errp)
{
    double val;
    char *ptr;

    val = strtod(kvs->value, &ptr);
    if (ptr && *ptr) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "<unknown>", "a valid double");
        return 0;
    }

    return val;
}

