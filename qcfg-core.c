#include "qcfg-core.h"
#include "qerror.h"
#include "qcfg-opts-core.h"

static const char *strchr_upto(const char *value, char ch, const char *end)
{
    while (*value && value != end && *value != ch) {
        value++;
    }

    return value;
}

static const char *strend(const char *value)
{
    while (*value) {
        value++;
    }
    return value;
}

static char *qemu_strdup_upto(const char *value, const char *end)
{
    size_t len = end - value;
    char *val = qemu_malloc(len + 1);

    memcpy(val, value, len);
    val[len] = 0;

    return val;
}

bool qcfg_iskey(const char *user_key, const char *key)
{
    const char *p;

    if (strcmp(user_key, key) == 0 ||
        (strstart(user_key, key, &p) && *p == '.')) {
            return true;
    }
    return false;
}

void qcfg_enhance_error(Error **errp, const char *name)
{
    Error *err;

    if (!errp) {
        return;
    }

    err = *errp;
    if (error_is_type(err, QERR_UNION_NO_VALUE) ||
        error_is_type(err, QERR_UNION_MULTIPLE_ENTRIES) ||
        error_is_type(err, QERR_ENUM_VALUE_INVALID) ||
        error_is_type(err, QERR_MISSING_PARAMETER) ||
        error_is_type(err, QERR_INVALID_PARAMETER)) {
        const char *old_name = error_get_field(err, "name");
        char new_name[1024];

        if (*old_name) {
            snprintf(new_name, sizeof(new_name), "%s.%s", name, old_name);
        } else {
            snprintf(new_name, sizeof(new_name), "%s", name);
        }

        error_set_field(err, "name", new_name);
    }
}

KeyValues *qcfg_parse(const char *value, const char *implicit_key)
{
    const char *ptr = value;
    KeyValues *kv_list = NULL;
    KeyValues **pkv = &kv_list;
    bool first = true;

    while (*ptr) {
        const char *end;
        const char *value;
        KeyValues *kv;

        kv = qapi_alloc_key_values();

        end = strchr(ptr, ',');
        if (end == NULL) {
            end = strend(ptr);
        }
        value = strchr_upto(ptr, '=', end);

        kv->key = qemu_strdup_upto(ptr, value);

        if (value == end) {
            if (first && implicit_key) {
                kv->value = kv->key;
                kv->key = qemu_strdup(implicit_key);
            } else {
                kv->value = qemu_strdup("on");
            }
        } else {
            // FIXME decode escaped stuff here
            kv->value = qemu_strdup_upto(value + 1, end);
        }

        ptr = end;
        if (*ptr) {
            ptr++;
        }

        *pkv = kv;
        pkv = &kv->next;

        first = false;
    }

    return kv_list;
}


// FIXME, always allocate
// always return a list of options for complex types
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
                KeyValues *new_kv = qapi_alloc_key_values();

                new_kv->key = qemu_strdup(p + 1);
                new_kv->value = qemu_strdup(kv->value);
                *retp = new_kv;
                retp = &new_kv->next;
                multi_key = true;
            }
        }
    }

    if (single_key && ret) {
        KeyValues *new_kv = qapi_alloc_key_values();
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

void qcfg_register_option_arg(const char *name, QcfgHandlerArg *fn)
{
}

void qcfg_register_option_noarg(const char *name, QcfgHandlerNoarg *fn)
{
}
