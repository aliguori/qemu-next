#ifndef QCFG_CORE_H
#define QCFG_CORE_H

#include "qemu-common.h"
#include "qcfg-types.h"
#include "qmp-types.h"
#include "qerror.h"

/**
 * Either returns:
 *   1) one KeyValue if the key is in the form, 'key'='value'
 *   2) returns a list of key/values if in the format 'key.subkey0'='value'
 *      where the new list is just 'subkey0'='value'
 */
KeyValues *qcfg_find_key(KeyValues *kvs, const char *key);
KeyValues *qcfg_parse(const char *value, const char *implicit_key);

void qcfg_enhance_error(Error **errp, const char *name);

char *qcfg_unmarshal_type_str(KeyValues *kvs, Error **errp);
bool qcfg_unmarshal_type_bool(KeyValues *kvs, Error **errp);
int64_t qcfg_unmarshal_type_int(KeyValues *kvs, Error **errp);
double qcfg_unmarshal_type_number(KeyValues *kvs, Error **errp);

#endif
